/*
  Created by Fabrizio Di Vittorio (fdivitto2013@gmail.com) - <http://www.fabgl.com>
  Copyright (c) 2019-2020 Fabrizio Di Vittorio.
  All rights reserved.

  This file is part of FabGL Library.

  FabGL is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  FabGL is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with FabGL.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

#include "diskio.h"
#include "ff.h"
#include "esp_vfs_fat.h"
#include "esp_task_wdt.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_spiffs.h"
#include "soc/efuse_reg.h"

#include "fabutils.h"
#include "dispdrivers/vgacontroller.h"
#include "comdrivers/ps2controller.h"



namespace fabgl {



////////////////////////////////////////////////////////////////////////////////////////////
// TimeOut


TimeOut::TimeOut()
  : m_start(esp_timer_get_time())
{
}


bool TimeOut::expired(int valueMS)
{
  return valueMS > -1 && ((esp_timer_get_time() - m_start) / 1000) > valueMS;
}



////////////////////////////////////////////////////////////////////////////////////////////
// isqrt

// Integer square root by Halleck's method, with Legalize's speedup
int isqrt (int x)
{
  if (x < 1)
    return 0;
  int squaredbit = 0x40000000;
  int remainder = x;
  int root = 0;
  while (squaredbit > 0) {
    if (remainder >= (squaredbit | root)) {
      remainder -= (squaredbit | root);
      root >>= 1;
      root |= squaredbit;
    } else
      root >>= 1;
    squaredbit >>= 2;
  }
  return root;
}



////////////////////////////////////////////////////////////////////////////////////////////
// calcParity

bool calcParity(uint8_t v)
{
  v ^= v >> 4;
  v &= 0xf;
  return (0x6996 >> v) & 1;
}


////////////////////////////////////////////////////////////////////////////////////////////
// realloc32
// free32

// size must be a multiple of uint32_t (32 bit)
void * realloc32(void * ptr, size_t size)
{
  uint32_t * newBuffer = (uint32_t*) heap_caps_malloc(size, MALLOC_CAP_32BIT);
  if (ptr) {
    moveItems(newBuffer, (uint32_t*)ptr, size / sizeof(uint32_t));
    heap_caps_free(ptr);
  }
  return newBuffer;
}


void free32(void * ptr)
{
  heap_caps_free(ptr);
}



////////////////////////////////////////////////////////////////////////////////////////////
// suspendInterrupts
// resumeInterrupts

void suspendInterrupts()
{
  if (VGAController::instance())
    VGAController::instance()->suspendBackgroundPrimitiveExecution();
  if (PS2Controller::instance())
    PS2Controller::instance()->suspend();
}


void resumeInterrupts()
{
  if (PS2Controller::instance())
    PS2Controller::instance()->resume();
  if (VGAController::instance())
    VGAController::instance()->resumeBackgroundPrimitiveExecution();
}


////////////////////////////////////////////////////////////////////////////////////////////
// msToTicks

uint32_t msToTicks(int ms)
{
  return ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(ms);
}


////////////////////////////////////////////////////////////////////////////////////////////
// getChipPackage

ChipPackage getChipPackage()
{
  // read CHIP_VER_PKG (block0, byte 3, 105th bit % 32 = 9, 3 bits)
  uint32_t ver_pkg = (REG_READ(EFUSE_BLK0_RDATA3_REG) >> 9) & 7;
  switch (ver_pkg) {
    case 0:
      return ChipPackage::ESP32D0WDQ6;
    case 1:
      return ChipPackage::ESP32D0WDQ5;
    case 2:
      return ChipPackage::ESP32D2WDQ5;
    case 5:
      return ChipPackage::ESP32PICOD4;
    default:
      return ChipPackage::Unknown;
  }
}


////////////////////////////////////////////////////////////////////////////////////////////
// Sutherland-Cohen line clipping algorithm

static int clipLine_code(int x, int y, Rect const & clipRect)
{
  int code = 0;
  if (x < clipRect.X1)
    code = 1;
  else if (x > clipRect.X2)
    code = 2;
  if (y < clipRect.Y1)
    code |= 4;
  else if (y > clipRect.Y2)
    code |= 8;
  return code;
}

// false = line is out of clipping rect
// true = line intersects or is inside the clipping rect (x1, y1, x2, y2 are changed if checkOnly=false)
bool clipLine(int & x1, int & y1, int & x2, int & y2, Rect const & clipRect, bool checkOnly)
{
  int newX1 = x1;
  int newY1 = y1;
  int newX2 = x2;
  int newY2 = y2;
  int topLeftCode     = clipLine_code(newX1, newY1, clipRect);
  int bottomRightCode = clipLine_code(newX2, newY2, clipRect);
  while (true) {
    if ((topLeftCode == 0) && (bottomRightCode == 0)) {
      if (!checkOnly) {
        x1 = newX1;
        y1 = newY1;
        x2 = newX2;
        y2 = newY2;
      }
      return true;
    } else if (topLeftCode & bottomRightCode) {
      break;
    } else {
      int x = 0, y = 0;
      int ncode = topLeftCode != 0 ? topLeftCode : bottomRightCode;
      if (ncode & 8) {
        x = newX1 + (newX2 - newX1) * (clipRect.Y2 - newY1) / (newY2 - newY1);
        y = clipRect.Y2;
      } else if (ncode & 4) {
        x = newX1 + (newX2 - newX1) * (clipRect.Y1 - newY1) / (newY2 - newY1);
        y = clipRect.Y1;
      } else if (ncode & 2) {
        y = newY1 + (newY2 - newY1) * (clipRect.X2 - newX1) / (newX2 - newX1);
        x = clipRect.X2;
      } else if (ncode & 1) {
        y = newY1 + (newY2 - newY1) * (clipRect.X1 - newX1) / (newX2 - newX1);
        x = clipRect.X1;
      }
      if (ncode == topLeftCode) {
        newX1 = x;
        newY1 = y;
        topLeftCode = clipLine_code(newX1, newY1, clipRect);
      } else {
        newX2 = x;
        newY2 = y;
        bottomRightCode = clipLine_code(newX2, newY2, clipRect);
      }
    }
  }
  return false;
}


////////////////////////////////////////////////////////////////////////////////////////////
// removeRectangle
// remove "rectToRemove" from "mainRect", pushing remaining rectangles to "rects" stack

void removeRectangle(Stack<Rect> & rects, Rect const & mainRect, Rect const & rectToRemove)
{
  if (!mainRect.intersects(rectToRemove) || rectToRemove.contains(mainRect))
    return;

  // top rectangle
  if (mainRect.Y1 < rectToRemove.Y1)
    rects.push(Rect(mainRect.X1, mainRect.Y1, mainRect.X2, rectToRemove.Y1 - 1));

  // bottom rectangle
  if (mainRect.Y2 > rectToRemove.Y2)
    rects.push(Rect(mainRect.X1, rectToRemove.Y2 + 1, mainRect.X2, mainRect.Y2));

  // left rectangle
  if (mainRect.X1 < rectToRemove.X1)
    rects.push(Rect(mainRect.X1, tmax(rectToRemove.Y1, mainRect.Y1), rectToRemove.X1 - 1, tmin(rectToRemove.Y2, mainRect.Y2)));

  // right rectangle
  if (mainRect.X2 > rectToRemove.X2)
    rects.push(Rect(rectToRemove.X2 + 1, tmax(rectToRemove.Y1, mainRect.Y1), mainRect.X2, tmin(rectToRemove.Y2, mainRect.Y2)));
}



///////////////////////////////////////////////////////////////////////////////////
// StringList


StringList::StringList()
  : m_items(nullptr),
    m_selMap(nullptr),
    m_ownStrings(false),
    m_count(0),
    m_allocated(0)
{
}


StringList::~StringList()
{
  clear();
}


void StringList::clear()
{
  if (m_ownStrings) {
    for (int i = 0; i < m_count; ++i)
      free((void*) m_items[i]);
  }
  free32(m_items);
  free32(m_selMap);
  m_items  = nullptr;
  m_selMap = nullptr;
  m_count  = m_allocated = 0;
}


void StringList::copyFrom(StringList const & src)
{
  clear();
  m_count = src.m_count;
  checkAllocatedSpace(m_count);
  for (int i = 0; i < m_count; ++i) {
    m_items[i] = nullptr;
    set(i, src.m_items[i]);
  }
  deselectAll();
}


void StringList::checkAllocatedSpace(int requiredItems)
{
  if (m_allocated < requiredItems) {
    if (m_allocated == 0) {
      // first time allocates exact space
      m_allocated = requiredItems;
    } else {
      // next times allocate double
      while (m_allocated < requiredItems)
        m_allocated *= 2;
    }
    m_items  = (char const**) realloc32(m_items, m_allocated * sizeof(char const *));
    m_selMap = (uint32_t*) realloc32(m_selMap, (31 + m_allocated) / 32 * sizeof(uint32_t));
  }
}


void StringList::insert(int index, char const * str)
{
  ++m_count;
  checkAllocatedSpace(m_count);
  moveItems(m_items + index + 1, m_items + index, m_count - index - 1);
  m_items[index] = nullptr;
  set(index, str);
  deselectAll();
}


int StringList::append(char const * str)
{
  insert(m_count, str);
  return m_count - 1;
}


void StringList::set(int index, char const * str)
{
  if (m_ownStrings) {
    free((void*)m_items[index]);
    m_items[index] = (char const*) malloc(strlen(str) + 1);
    strcpy((char*)m_items[index], str);
  } else {
    m_items[index] = str;
  }
}


void StringList::remove(int index)
{
  if (m_ownStrings)
    free((void*)m_items[index]);
  moveItems(m_items + index, m_items + index + 1, m_count - index - 1);
  --m_count;
  deselectAll();
}


void StringList::takeStrings()
{
  if (!m_ownStrings) {
    m_ownStrings = true;
    // take existing strings
    for (int i = 0; i < m_count; ++i) {
      char const * str = m_items[i];
      m_items[i] = nullptr;
      set(i, str);
    }
  }
}


void StringList::deselectAll()
{
  for (int i = 0; i < (31 + m_count) / 32; ++i)
    m_selMap[i] = 0;
}


bool StringList::selected(int index)
{
  return m_selMap[index / 32] & (1 << (index % 32));
}


void StringList::select(int index, bool value)
{
  if (value)
    m_selMap[index / 32] |= 1 << (index % 32);
  else
    m_selMap[index / 32] &= ~(1 << (index % 32));
}



// StringList
///////////////////////////////////////////////////////////////////////////////////



///////////////////////////////////////////////////////////////////////////////////
// FileBrowser

FileBrowser::FileBrowser()
  : m_dir(nullptr),
    m_count(0),
    m_items(nullptr),
    m_sorted(true),
    m_includeHiddenFiles(false),
    m_namesStorage(nullptr)
{
}


FileBrowser::~FileBrowser()
{
  clear();
}


void FileBrowser::clear()
{
  free(m_items);
  m_items = nullptr;

  free(m_namesStorage);
  m_namesStorage = nullptr;

  m_count = 0;
}


// set absolute directory (full path must be specified)
void FileBrowser::setDirectory(const char * path)
{
  free(m_dir);
  m_dir = strdup(path);
  reload();
}


// set relative directory:
//   ".." : go to the parent directory
//   "dirname": go inside the specified sub directory
void FileBrowser::changeDirectory(const char * subdir)
{
  if (!m_dir)
    return;
  if (strcmp(subdir, "..") == 0) {
    // go to parent directory
    auto lastSlash = strrchr(m_dir, '/');
    if (lastSlash && lastSlash != m_dir) {
      *lastSlash = 0;
      reload();
    }
  } else {
    // go to sub directory
    int oldLen = strlen(m_dir);
    char * newDir = (char*) malloc(oldLen + 1 + strlen(subdir) + 1);  // m_dir + '/' + subdir + 0
    strcpy(newDir, m_dir);
    newDir[oldLen] = '/';
    strcpy(newDir + oldLen + 1, subdir);
    free(m_dir);
    m_dir = newDir;
    reload();
  }
}


int FileBrowser::countDirEntries(int * namesLength)
{
  int c = 0;
  *namesLength = 0;
  if (m_dir) {
    AutoSuspendInterrupts autoInt;
    auto dirp = opendir(m_dir);
    while (dirp) {
      auto dp = readdir(dirp);
      if (dp == NULL)
        break;
      if (strcmp(".", dp->d_name) && strcmp("..", dp->d_name) && dp->d_type != DT_UNKNOWN) {
        *namesLength += strlen(dp->d_name) + 1;
        ++c;
      }
    }
    closedir(dirp);
  }
  return c;
}


bool FileBrowser::exists(char const * name)
{
  for (int i = 0; i < m_count; ++i)
    if (strcmp(name, m_items[i].name) == 0)
      return true;
  return false;
}


int DirComp(const void * i1, const void * i2)
{
  DirItem * d1 = (DirItem*)i1;
  DirItem * d2 = (DirItem*)i2;
  if (d1->isDir != d2->isDir) // directories first
    return d1->isDir ? -1 : +1;
  else
    return strcmp(d1->name, d2->name);
}


void FileBrowser::reload()
{
  clear();
  int namesAlloc;
  int c = countDirEntries(&namesAlloc);
  m_items = (DirItem*) malloc(sizeof(DirItem) * (c + 1));
  m_namesStorage = (char*) malloc(namesAlloc);
  char * sname = m_namesStorage;

  // first item is always ".."
  m_items[0].name  = "..";
  m_items[0].isDir = true;
  ++m_count;

  AutoSuspendInterrupts autoInt;
  auto dirp = opendir(m_dir);
  for (int i = 0; i < c; ++i) {
    auto dp = readdir(dirp);
    if (strcmp(".", dp->d_name) && strcmp("..", dp->d_name) && dp->d_type != DT_UNKNOWN) {
      DirItem * di = m_items + m_count;
      // check if this is a simulated directory (like in SPIFFS)
      auto slashPos = strchr(dp->d_name, '/');
      if (slashPos) {
        // yes, this is a simulated dir. Trunc and avoid to insert it twice
        int len = slashPos - dp->d_name;
        strncpy(sname, dp->d_name, len);
        sname[len] = 0;
        if (!exists(sname)) {
          di->name  = sname;
          di->isDir = true;
          sname += len + 1;
          ++m_count;
        }
      } else if (m_includeHiddenFiles || dp->d_name[0] != '.') {
        strcpy(sname, dp->d_name);
        di->name  = sname;
        di->isDir = (dp->d_type == DT_DIR);
        sname += strlen(sname) + 1;
        ++m_count;
      }
    }
  }
  closedir(dirp);
  if (m_sorted)
    qsort(m_items, m_count, sizeof(DirItem), DirComp);
}


// note: for SPIFFS this creates an empty ".dirname" file. The SPIFFS path is detected when path starts with "/spiffs"
// "dirname" is not a path, just a directory name created inside "m_dir"
void FileBrowser::makeDirectory(char const * dirname)
{
  int dirnameLen = strlen(dirname);
  if (dirnameLen > 0) {
    AutoSuspendInterrupts autoInt;
    if (strncmp(m_dir, "/spiffs", 7) == 0) {
      // simulated directory, puts an hidden placeholder
      char fullpath[strlen(m_dir) + 3 + 2 * dirnameLen + 1];
      sprintf(fullpath, "%s/%s/.%s", m_dir, dirname, dirname);
      FILE * f = fopen(fullpath, "wb");
      fclose(f);
    } else {
      char fullpath[strlen(m_dir) + 1 + dirnameLen + 1];
      sprintf(fullpath, "%s/%s", m_dir, dirname);
      mkdir(fullpath, ACCESSPERMS);
    }
  }
}


// removes a file or a directory (and all files inside it)
// The SPIFFS path is detected when path starts with "/spiffs"
// "name" is not a path, just a file or directory name inside "m_dir"
void FileBrowser::remove(char const * name)
{
  AutoSuspendInterrupts autoInt;

  char fullpath[strlen(m_dir) + 1 + strlen(name) + 1];
  sprintf(fullpath, "%s/%s", m_dir, name);
  int r = unlink(fullpath);

  if (r != 0) {
    // failed
    if (strncmp(m_dir, "/spiffs", 7) == 0) {
      // simulated directory
      // maybe this is a directory, remove ".dir" file
      char hidpath[strlen(m_dir) + 3 + 2 * strlen(name) + 1];
      sprintf(hidpath, "%s/%s/.%s", m_dir, name, name);
      unlink(hidpath);
      // maybe the directory contains files, remove all
      auto dirp = opendir(fullpath);
      while (dirp) {
        auto dp = readdir(dirp);
        if (dp == NULL)
          break;
        if (strcmp(".", dp->d_name) && strcmp("..", dp->d_name) && dp->d_type != DT_UNKNOWN) {
          char sfullpath[strlen(fullpath) + 1 + strlen(dp->d_name) + 1];
          sprintf(sfullpath, "%s/%s", fullpath, dp->d_name);
          unlink(sfullpath);
        }
      }
      closedir(dirp);
    }
  }
}


// works only for files
void FileBrowser::rename(char const * oldName, char const * newName)
{
  AutoSuspendInterrupts autoInt;

  char oldfullpath[strlen(m_dir) + 1 + strlen(oldName) + 1];
  sprintf(oldfullpath, "%s/%s", m_dir, oldName);

  char newfullpath[strlen(m_dir) + 1 + strlen(newName) + 1];
  sprintf(newfullpath, "%s/%s", m_dir, newName);

  ::rename(oldfullpath, newfullpath);
}


// concatenates current directory and specified name and store result into fullpath
// Specifying outPath=nullptr returns required length
int FileBrowser::getFullPath(char const * name, char * outPath, int maxlen)
{
  return outPath ? snprintf(outPath, maxlen, "%s/%s", m_dir, name) : snprintf(nullptr, 0, "%s/%s", m_dir, name) + 1;
}


bool FileBrowser::format(DriveType driveType, int drive)
{
  AutoSuspendInterrupts autoSuspendInt;

  esp_task_wdt_init(45, false);

  if (driveType == DriveType::SDCard) {

    // unmount filesystem
    char drv[3] = {(char)('0' + drive), ':', 0};
    f_mount(0, drv, 0);

    void * buffer = malloc(FF_MAX_SS);
    if (!buffer)
      return false;

    // create partition
    DWORD plist[] = { 100, 0, 0, 0 };
    if (f_fdisk(drive, plist, buffer) != FR_OK) {
      free(buffer);
      return false;
    }

    // make filesystem
    if (f_mkfs(drv, FM_ANY, 16 * 1024, buffer, FF_MAX_SS) != FR_OK) {
      free(buffer);
      return false;
    }

    free(buffer);

    return true;

  } else {

    // driveType == DriveType::SPIFFS
    return esp_spiffs_format(nullptr) == ESP_OK;

  }
}


bool FileBrowser::mountSDCard(bool formatOnFail, char const * mountPath, int maxFiles, int allocationUnitSize, int MISO, int MOSI, int CLK, int CS)
{
  if (getChipPackage() == ChipPackage::ESP32PICOD4 && (MISO == 16 || MOSI == 17))
    return false; // PICO-D4 uses pins 16 and 17 for Flash
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  sdspi_slot_config_t slot_config = SDSPI_SLOT_CONFIG_DEFAULT();
  slot_config.gpio_miso = int2gpio(MISO);
  slot_config.gpio_mosi = int2gpio(MOSI);
  slot_config.gpio_sck  = int2gpio(CLK);
  slot_config.gpio_cs   = int2gpio(CS);
  esp_vfs_fat_sdmmc_mount_config_t mount_config;
  mount_config.format_if_mount_failed = formatOnFail;
  mount_config.max_files = maxFiles;
  mount_config.allocation_unit_size = allocationUnitSize;
  sdmmc_card_t* card;
  return esp_vfs_fat_sdmmc_mount(mountPath, &host, &slot_config, &mount_config, &card) == ESP_OK;
}


void FileBrowser::unmountSDCard()
{
  esp_vfs_fat_sdmmc_unmount();
}


bool FileBrowser::mountSPIFFS(bool formatOnFail, char const * mountPath, int maxFiles)
{
  esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = nullptr,
      .max_files = 4,
      .format_if_mount_failed = true
  };
  AutoSuspendInterrupts autoSuspendInt;
  return esp_vfs_spiffs_register(&conf) == ESP_OK;
}


void FileBrowser::unmountSPIFFS()
{
  esp_vfs_spiffs_unregister(nullptr);
}


bool FileBrowser::getFSInfo(DriveType driveType, int drive, int64_t * total, int64_t * used)
{
  if (driveType == DriveType::SDCard) {

    FATFS * fs;
    DWORD free_clusters;
    char drv[3] = {(char)('0' + drive), ':', 0};
    if (f_getfree(drv, &free_clusters, &fs) != FR_OK)
      return false;
    int64_t total_sectors = (fs->n_fatent - 2) * fs->csize;
    int64_t free_sectors = free_clusters * fs->csize;
    *total = total_sectors * fs->ssize;
    *used = *total - free_sectors * fs->ssize;

    return true;

  } else {

    // driveType == DriveType::SPIFFS
    *total = *used = 0;
    size_t stotal = 0, sused = 0;
    if (esp_spiffs_info(NULL, &stotal, &sused) != ESP_OK)
      return false;
    *total = stotal;
    *used  = sused;
    return true;

  }
}


// FileBrowser
///////////////////////////////////////////////////////////////////////////////////




///////////////////////////////////////////////////////////////////////////////////
// LightMemoryPool


void LightMemoryPool::mark(int pos, int16_t size, bool allocated)
{
  m_mem[pos]     = size & 0xff;
  m_mem[pos + 1] = ((size >> 8) & 0x7f) | (allocated ? 0x80 : 0);
}


int16_t LightMemoryPool::getSize(int pos)
{
  return m_mem[pos] | ((m_mem[pos + 1] & 0x7f) << 8);
}


bool LightMemoryPool::isFree(int pos)
{
  return (m_mem[pos + 1] & 0x80) == 0;
}


LightMemoryPool::LightMemoryPool(int poolSize)
{
  m_poolSize = poolSize + 2;
  m_mem = (uint8_t*) malloc(m_poolSize);
  mark(0, m_poolSize - 2, false);
}


LightMemoryPool::~LightMemoryPool()
{
  ::free(m_mem);
}


void * LightMemoryPool::alloc(int size)
{
  for (int pos = 0; pos < m_poolSize; ) {
    int16_t blockSize = getSize(pos);
    if (isFree(pos)) {
      if (blockSize == size) {
        // found a block having the same size
        mark(pos, size, true);
        return m_mem + pos + 2;
      } else if (blockSize > size) {
        // found a block having larger size
        int remainingSize = blockSize - size - 2;
        if (remainingSize > 0)
          mark(pos + 2 + size, remainingSize, false);  // create new free block at the end of this block
        else
          size = blockSize; // to avoid to waste last block
        mark(pos, size, true);  // reduce size of this block and mark as allocated
        return m_mem + pos + 2;
      } else {
        // this block hasn't enough space
        // can merge with next block?
        int nextBlockPos = pos + 2 + blockSize;
        if (nextBlockPos < m_poolSize && isFree(nextBlockPos)) {
          // join blocks and stay at this pos
          mark(pos, blockSize + getSize(nextBlockPos) + 2, false);
        } else {
          // move to the next block
          pos += blockSize + 2;
        }
      }
    } else {
      // move to the next block
      pos += blockSize + 2;
    }
  }
  return nullptr;
}


bool LightMemoryPool::memCheck()
{
  int pos = 0;
  while (pos < m_poolSize) {
    int16_t blockSize = getSize(pos);
    pos += blockSize + 2;
  }
  return pos == m_poolSize;
}


int LightMemoryPool::totFree()
{
  int r = 0;
  for (int pos = 0; pos < m_poolSize; ) {
    int16_t blockSize = getSize(pos);
    if (isFree(pos))
      r += blockSize;
    pos += blockSize + 2;
  }
  return r;
}


int LightMemoryPool::totAllocated()
{
  int r = 0;
  for (int pos = 0; pos < m_poolSize; ) {
    int16_t blockSize = getSize(pos);
    if (!isFree(pos))
      r += blockSize;
    pos += blockSize + 2;
  }
  return r;
}


int LightMemoryPool::largestFree()
{
  int r = 0;
  for (int pos = 0; pos < m_poolSize; ) {
    int16_t blockSize = getSize(pos);
    if (isFree(pos) && blockSize > r)
      r = blockSize;
    pos += blockSize + 2;
  }
  return r;
}


// LightMemoryPool
///////////////////////////////////////////////////////////////////////////////////


}

