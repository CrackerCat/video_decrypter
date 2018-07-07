// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "native_library.h"

#include <windows.h>

//#include "base/files/file_util.h"
//#include "base/strings/stringprintf.h"
//#include "base/strings/utf_string_conversions.h"
//#include "base/threading/thread_restrictions.h"

namespace base {

#if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
typedef HMODULE (WINAPI* LoadLibraryFunction)(const char* file_name, unsigned long res);
#else
typedef HMODULE (WINAPI* LoadLibraryFunction)(const char* file_name);
#endif

namespace {

NativeLibrary LoadNativeLibraryHelper(const std::string& library_path,
                                      LoadLibraryFunction load_library_api,
                                      NativeLibraryLoadError* error) {
  // LoadLibrary() opens the file off disk.
  //ThreadRestrictions::AssertIOAllowed();

  // Switch the current directory to the library directory as the library
  // may have dependencies on DLLs in this directory.
  bool restore_directory = false;
  char current_directory[MAX_PATH];
  std::string lp = std::string(library_path.begin(), library_path.end());
  std::string plugin_path, plugin_value;
  if (GetCurrentDirectoryA(MAX_PATH,current_directory))
  {
	const char *res = strrchr(lp.c_str(), '/');
	if (res)
  {
    plugin_path.assign(lp.c_str(), res);
    const char *tmp = strrchr(res, 0);
    plugin_value.assign(++res, tmp);
  }
  else
	  plugin_value = lp;

	if (!plugin_path.empty())
	{
      SetCurrentDirectoryA((char*)plugin_path.c_str());
      restore_directory = true;
    }
  }

#if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
  HMODULE module = (*load_library_api)((char*)plugin_value.c_str(), 0);
#else
  HMODULE module = (*load_library_api)((char*)library_path.c_str());
#endif
  if (!module && error) {
    // GetLastError() needs to be called immediately after |load_library_api|.
    error->code = GetLastError();
  }

  if (restore_directory)
    SetCurrentDirectoryA(current_directory);

  return module;
}

}  // namespace

std::string NativeLibraryLoadError::ToString() const
{
	char buf[32];
	return int2char(code, buf);
}

// static
NativeLibrary LoadNativeLibrary(const std::string& library_path,
                                NativeLibraryLoadError* error) {
#if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
  return LoadNativeLibraryHelper(library_path, LoadPackagedLibrary, error);
#else
  return LoadNativeLibraryHelper(library_path, LoadLibraryA, error);
#endif
}

NativeLibrary LoadNativeLibraryDynamically(const std::string& library_path) {
  typedef HMODULE (WINAPI* LoadLibraryFunction)(const char* file_name);

#if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
  return LoadNativeLibraryHelper(library_path, LoadPackagedLibrary, NULL);
#else
  LoadLibraryFunction load_library;
  load_library = reinterpret_cast<LoadLibraryFunction>(
      GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA"));

  return LoadNativeLibraryHelper(library_path, load_library, NULL);
#endif
}

// static
void UnloadNativeLibrary(NativeLibrary library) {
  FreeLibrary(library);
}

// static
void* GetFunctionPointerFromNativeLibrary(NativeLibrary library,
                                          const char* name) {
  return (void*) GetProcAddress(library, name);
}

// static
//string16 GetNativeLibraryName(const string16& name) {
//  return name + ASCIIToUTF16(".dll");
//}

}  // namespace base
