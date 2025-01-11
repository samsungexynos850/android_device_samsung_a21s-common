#!/bin/env python3
#
# Copyright (C) 2020-2025 The LineageOS Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import common
import re

def FullOTA_Assertions(info):
  OTA_Assertions(info)
  return

def FullOTA_InstallEnd(info):
  OTA_InstallEnd(info)
  return

def IncrementalOTA_Assertions(info):
  OTA_Assertions(info)
  return

def IncrementalOTA_InstallEnd(info):
  OTA_InstallEnd(info)
  return

def AddImage(info, basename, dest):
  name = basename
  data = info.input_zip.read("IMAGES/" + basename)
  common.ZipWriteStr(info.output_zip, name, data)
  info.script.AppendExtra('package_extract_file("%s", "%s");' % (name, dest))

def OTA_Assertions(info):
  android_info = info.input_zip.read("OTA/android-info.txt").decode("utf-8")
  m = re.search(r'require\s+version-bootloader-min\s*=\s*(\S+)', android_info)
  if m:
    bootloader_version = m.group(1)
    cmd = ('assert(exynos850.verify_bootloader_min("{}") == "1" || abort("ERROR: This package requires binary C based firmware. Please upgrade firmware and retry!"););').format(bootloader_version)
    info.script.AppendExtra(cmd)
  return

def PrintInfo(info, dest):
  info.script.Print("Patching {} image unconditionally...".format(dest.split('/')[-1]))

def OTA_InstallEnd(info):
  PrintInfo(info, "/dev/block/by-name/dtbo")
  AddImage(info, "dtbo.img", "/dev/block/by-name/dtbo")
  PrintInfo(info, "/dev/block/by-name/vbmeta")
  AddImage(info, "vbmeta.img", "/dev/block/by-name/vbmeta")
  return
