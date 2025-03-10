// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/obsolete_system/obsolete_system.h"

#if defined(GOOGLE_CHROME_BUILD) && !defined(OS_CHROMEOS)
#include <gnu/libc-version.h>

#include "base/sys_info.h"
#include "base/version.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/chromium_strings.h"
#include "ui/base/l10n/l10n_util.h"
#endif

// static
bool ObsoleteSystem::IsObsoleteNowOrSoon() {
#if defined(GOOGLE_CHROME_BUILD) && !defined(OS_CHROMEOS)
#if defined(ARCH_CPU_32_BITS)
  return true;
#else
  // Ubuntu 14.04 will be used as the next build platform, and it ships with
  // glibc 2.19, so check for that as the minimum requirement.
  Version version(gnu_get_libc_version());
  if (!version.IsValid() || version.components().size() != 2)
    return false;

  uint32_t glibc_major_version = version.components()[0];
  uint32_t glibc_minor_version = version.components()[1];
  if (glibc_major_version < 2)
    return true;

  return glibc_major_version == 2 && glibc_minor_version < 19;
#endif  // defined(ARCH_CPU_32_BITS)
#else
  return false;
#endif  // defined(GOOGLE_CHROME_BUILD) && !defined(OS_CHROMEOS)
}

// static
base::string16 ObsoleteSystem::LocalizedObsoleteString() {
#if defined(GOOGLE_CHROME_BUILD) && !defined(OS_CHROMEOS)
  const bool is_eol = IsEndOfTheLine();
  int id = is_eol ? IDS_LINUX_WHEEZY_PRECISE_OBSOLETE_NOW
                  : IDS_LINUX_WHEEZY_PRECISE_OBSOLETE_SOON;
#if defined(ARCH_CPU_32_BITS)
  // On 32-bit builds, check if the kernel is 64-bit. If so, tell users to
  // switch to 64-bit Chrome.
  if (base::SysInfo::OperatingSystemArchitecture() == "x86_64") {
    id = is_eol ? IDS_LINUX_WHEEZY_PRECISE_OBSOLETE_NOW_CAN_UPGRADE
                : IDS_LINUX_WHEEZY_PRECISE_OBSOLETE_SOON_CAN_UPGRADE;
  }
#endif  // defined(ARCH_CPU_32_BITS)
  return l10n_util::GetStringUTF16(id);
#else
  return base::string16();
#endif  // defined(GOOGLE_CHROME_BUILD) && !defined(OS_CHROMEOS)
}

// static
bool ObsoleteSystem::IsEndOfTheLine() {
  return false;
}

// static
const char* ObsoleteSystem::GetLinkURL() {
#if defined(GOOGLE_CHROME_BUILD) && !defined(OS_CHROMEOS)
  return chrome::kLinuxWheezyPreciseDeprecationURL;
#else
  return "";
#endif
}
