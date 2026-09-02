// Unpacking the package.
//
// The one part of the core that touches a disk. A Unification package is
// hundreds of megabytes, and a rule that made the host hold one in memory to
// hand it across the ABI would be a rule against working on the machines this
// program exists for — so this streams, and it only ever writes below the
// directory it is given.
//
// The decoder is Alexander Roshal's UnRAR, vendored under thirdparty/unrar and
// used through its own embedding API rather than by shelling out to a tool.
// Shelling out to the tar.exe in System32 does work — that is libarchive, and it
// reads rar4 and rar5 — but it only exists from Windows 10 1803, and a good
// share of the people still playing Warcraft II online are not on that.
//
// Two things are refused rather than worked around:
//
//   A path that escapes the destination. "../../Windows/System32/x.dll" inside
//   an archive is not a packaging mistake to be tidied up, and an install that
//   sanitised it and carried on would be an install that half-happened.
//
//   A password. A mod package is not encrypted, and prompting for a password
//   would only ever be answering a question nobody asked.

#include "uniloader/uniloader.h"

#include "util.hpp"

// The decoder's own headers, quietened. They are third-party code that compiles
// clean under its own makefile and noisily under /W4, and a wall of warnings
// nobody here can act on is a wall that hides the ones somebody can. Scoped to
// these two includes so the rest of this file keeps every warning it has.
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include "thirdparty/unrar/rar.hpp"
#include "thirdparty/unrar/dll.hpp"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <new>
#include <string>
#include <vector>

namespace ul {
namespace {

/// unrar's own converters, so this file does not carry a second implementation
/// of UTF-8 that could disagree with the one the decoder used to read the name.
std::string FromWide(const wchar_t* wide) {
  if (!wide || !*wide) return {};
  std::vector<char> buffer(std::char_traits<wchar_t>::length(wide) * 4 + 4);
  WideToUtf(wide, buffer.data(), buffer.size());
  return std::string(buffer.data());
}

std::wstring ToWide(const std::string& utf8) {
  if (utf8.empty()) return {};
  std::vector<wchar_t> buffer(utf8.size() + 2);
  UtfToWide(utf8.c_str(), buffer.data(), buffer.size());
  return std::wstring(buffer.data());
}

int64_t EntrySize(const RARHeaderDataEx& header) {
  return (static_cast<int64_t>(header.UnpSizeHigh) << 32) |
         static_cast<int64_t>(header.UnpSize);
}

int TranslateOpenError(unsigned int result) {
  switch (result) {
    case ERAR_NO_MEMORY:       return UL_ERR_OPEN;
    case ERAR_BAD_ARCHIVE:     return UL_ERR_FORMAT;
    case ERAR_UNKNOWN_FORMAT:  return UL_ERR_FORMAT;
    case ERAR_EOPEN:           return UL_ERR_OPEN;
    case ERAR_MISSING_PASSWORD:
    case ERAR_BAD_PASSWORD:    return UL_ERR_ENCRYPTED;
    default:                   return UL_ERR_OPEN;
  }
}

int TranslateProcessError(int result) {
  switch (result) {
    case ERAR_BAD_DATA:        return UL_ERR_FORMAT;
    case ERAR_BAD_ARCHIVE:     return UL_ERR_FORMAT;
    case ERAR_UNKNOWN_FORMAT:  return UL_ERR_FORMAT;
    case ERAR_ECREATE:
    case ERAR_EWRITE:
    case ERAR_ECLOSE:          return UL_ERR_WRITE;
    case ERAR_EOPEN:
    case ERAR_EREAD:           return UL_ERR_OPEN;
    case ERAR_MISSING_PASSWORD:
    case ERAR_BAD_PASSWORD:    return UL_ERR_ENCRYPTED;
    case ERAR_LARGE_DICT:      return UL_ERR_FORMAT;
    default:                   return UL_ERR_FORMAT;
  }
}

struct Entry {
  std::string name;      // package-relative, forward slashes
  int64_t size = 0;
  bool directory = false;
};

/// State the unrar callback reaches through. One per operation, never shared:
/// unrar hands the pointer straight back, so this is the whole of the coupling.
struct CallbackState {
  ul_archive_progress progress = nullptr;
  void* user = nullptr;
  const char* entry = "";
  int64_t done = 0;
  int64_t total = 0;
  bool cancelled = false;
  std::string* capture = nullptr;   // set when reading an entry into memory
};

int CALLBACK OnCallback(UINT message, LPARAM user, LPARAM p1, LPARAM p2) {
  auto* state = reinterpret_cast<CallbackState*>(user);
  switch (message) {
    case UCM_PROCESSDATA: {
      if (!state) return 1;
      const char* data = reinterpret_cast<const char*>(p1);
      const size_t length = static_cast<size_t>(p2);
      if (state->capture && data) state->capture->append(data, length);
      state->done += static_cast<int64_t>(length);
      if (state->progress &&
          !state->progress(state->user, state->entry, state->done, state->total)) {
        state->cancelled = true;
        return -1;
      }
      return 1;
    }
    case UCM_NEEDPASSWORD:
    case UCM_NEEDPASSWORDW:
      // -1 rather than an empty password: an empty one is an *attempt*, and
      // unrar reports the failure as bad data rather than as encryption.
      return -1;
    case UCM_CHANGEVOLUME:
    case UCM_CHANGEVOLUMEW:
      // A multi-volume package whose other parts are not beside it. Refusing
      // here turns "extracted half a mod" into a plain, reported failure.
      return p2 == RAR_VOL_ASK ? -1 : 1;
    default:
      return 1;
  }
}

}  // namespace
}  // namespace ul

struct ul_archive {
  std::string path;
  std::vector<ul::Entry> entries;
  int64_t total = 0;
};

namespace ul {
namespace {

/// Opens, and walks the headers. `collect` is null when the caller only wants
/// the handle — RAROpenArchiveEx has no seek, so listing and extracting are two
/// passes over the file, which for a local archive costs a header read each.
HANDLE OpenArchive(const std::string& path, int mode, int* error) {
  RAROpenArchiveDataEx data;
  std::memset(&data, 0, sizeof(data));
  std::wstring wide = ToWide(path);
  data.ArcNameW = wide.empty() ? nullptr : &wide[0];
  data.OpenMode = static_cast<unsigned int>(mode);
  HANDLE handle = RAROpenArchiveEx(&data);
  if (!handle || data.OpenResult != ERAR_SUCCESS) {
    if (handle) RARCloseArchive(handle);
    if (error) *error = TranslateOpenError(data.OpenResult);
    return nullptr;
  }
  if (error) *error = UL_OK;
  return handle;
}

}  // namespace
}  // namespace ul

// ------------------------------------------------------------------- the ABI

extern "C" {

ul_archive* ul_archive_open(const char* archive_path) {
  if (!archive_path || !*archive_path) {
    ul::SetLastError("No archive was named.");
    return nullptr;
  }
  int error = UL_OK;
  HANDLE handle = ul::OpenArchive(archive_path, RAR_OM_LIST, &error);
  if (!handle) {
    ul::SetLastError(ul_error_text(error));
    return nullptr;
  }

  auto archive = new (std::nothrow) ul_archive();
  if (!archive) {
    RARCloseArchive(handle);
    return nullptr;
  }
  archive->path = archive_path;

  RARHeaderDataEx header;
  for (;;) {
    std::memset(&header, 0, sizeof(header));
    const int read = RARReadHeaderEx(handle, &header);
    if (read == ERAR_END_ARCHIVE) break;
    if (read != ERAR_SUCCESS) {
      RARCloseArchive(handle);
      delete archive;
      ul::SetLastError(ul_error_text(ul::TranslateProcessError(read)));
      return nullptr;
    }
    if ((header.Flags & RHDF_ENCRYPTED) != 0) {
      RARCloseArchive(handle);
      delete archive;
      ul::SetLastError(ul_error_text(UL_ERR_ENCRYPTED));
      return nullptr;
    }
    ul::Entry entry;
    entry.name = ul::NormaliseSlashes(ul::FromWide(header.FileNameW));
    entry.size = ul::EntrySize(header);
    entry.directory = (header.Flags & RHDF_DIRECTORY) != 0;
    if (!entry.directory && !entry.name.empty()) {
      archive->total += entry.size;
      archive->entries.push_back(std::move(entry));
    }
    const int skipped = RARProcessFileW(handle, RAR_SKIP, nullptr, nullptr);
    if (skipped != ERAR_SUCCESS) {
      RARCloseArchive(handle);
      delete archive;
      ul::SetLastError(ul_error_text(ul::TranslateProcessError(skipped)));
      return nullptr;
    }
  }
  RARCloseArchive(handle);
  return archive;
}

void ul_archive_close(ul_archive* a) { delete a; }

int ul_archive_count(const ul_archive* a) {
  return a ? static_cast<int>(a->entries.size()) : 0;
}

const char* ul_archive_entry(const ul_archive* a, int index) {
  if (!a || index < 0 || index >= static_cast<int>(a->entries.size())) return "";
  return a->entries[static_cast<size_t>(index)].name.c_str();
}

int64_t ul_archive_entry_size(const ul_archive* a, int index) {
  if (!a || index < 0 || index >= static_cast<int>(a->entries.size())) return -1;
  return a->entries[static_cast<size_t>(index)].size;
}

int64_t ul_archive_total_size(const ul_archive* a) { return a ? a->total : 0; }

int ul_archive_extract(ul_archive* a, const char* dest_dir,
                       ul_archive_progress progress, void* user) {
  if (!a || !dest_dir || !*dest_dir) return UL_ERR_OPEN;

  // Every name is checked before a single byte is written. Finding the bad one
  // halfway through would leave a game folder that is neither the old one nor
  // the new one, and nothing in a plan could put that back.
  for (const ul::Entry& entry : a->entries) {
    if (!ul::IsSafeRelativePath(entry.name)) {
      ul::SetLastError("The archive contains \"" + entry.name +
                       "\", which points outside the folder it would be unpacked "
                       "into. Nothing was unpacked.");
      return UL_ERR_UNSAFE_PATH;
    }
  }

  int error = UL_OK;
  HANDLE handle = ul::OpenArchive(a->path, RAR_OM_EXTRACT, &error);
  if (!handle) {
    ul::SetLastError(ul_error_text(error));
    return error;
  }

  ul::CallbackState state;
  state.progress = progress;
  state.user = user;
  state.total = a->total;
  RARSetCallback(handle, ul::OnCallback, reinterpret_cast<LPARAM>(&state));

  std::wstring destination = ul::ToWide(ul::TrimTrailingSlash(dest_dir));
  RARHeaderDataEx header;
  int result = UL_OK;
  for (;;) {
    std::memset(&header, 0, sizeof(header));
    const int read = RARReadHeaderEx(handle, &header);
    if (read == ERAR_END_ARCHIVE) break;
    if (read != ERAR_SUCCESS) {
      result = ul::TranslateProcessError(read);
      break;
    }
    const std::string name = ul::NormaliseSlashes(ul::FromWide(header.FileNameW));
    // Re-checked against the extraction pass's own headers rather than trusting
    // the list built earlier: the two are separate reads of the file, and it is
    // the name used *here* that decides where a byte lands.
    if (!name.empty() && !ul::IsSafeRelativePath(name)) {
      ul::SetLastError("The archive contains \"" + name +
                       "\", which points outside the folder it is unpacked into.");
      result = UL_ERR_UNSAFE_PATH;
      break;
    }
    state.entry = name.c_str();
    const int processed = RARProcessFileW(
        handle, RAR_EXTRACT, destination.empty() ? nullptr : &destination[0], nullptr);
    if (processed != ERAR_SUCCESS) {
      result = state.cancelled ? UL_ERR_CANCELLED : ul::TranslateProcessError(processed);
      break;
    }
    if (state.cancelled) {
      result = UL_ERR_CANCELLED;
      break;
    }
  }
  RARCloseArchive(handle);
  if (result != UL_OK && result != UL_ERR_UNSAFE_PATH && result != UL_ERR_CANCELLED) {
    ul::SetLastError(ul_error_text(result));
  }
  return result;
}

char* ul_archive_read_entry(ul_archive* a, int index, size_t* length) {
  if (length) *length = 0;
  if (!a || index < 0 || index >= static_cast<int>(a->entries.size())) return nullptr;
  const std::string wanted = a->entries[static_cast<size_t>(index)].name;

  int error = UL_OK;
  HANDLE handle = ul::OpenArchive(a->path, RAR_OM_EXTRACT, &error);
  if (!handle) {
    ul::SetLastError(ul_error_text(error));
    return nullptr;
  }

  // RAR_TEST with a capturing callback, which is how the entry is read without
  // a temporary file. The pointer archive on the mod page is 197 bytes whose
  // whole purpose is the text inside, and writing that to disk to read it back
  // would be the only reason this program ever needed a scratch folder.
  std::string captured;
  ul::CallbackState state;
  state.capture = &captured;
  state.total = a->entries[static_cast<size_t>(index)].size;
  RARSetCallback(handle, ul::OnCallback, reinterpret_cast<LPARAM>(&state));

  RARHeaderDataEx header;
  bool found = false;
  for (;;) {
    std::memset(&header, 0, sizeof(header));
    const int read = RARReadHeaderEx(handle, &header);
    if (read != ERAR_SUCCESS) break;
    const std::string name = ul::NormaliseSlashes(ul::FromWide(header.FileNameW));
    const int operation = ul::EqualsNoCase(name, wanted) ? RAR_TEST : RAR_SKIP;
    if (RARProcessFileW(handle, operation, nullptr, nullptr) != ERAR_SUCCESS) break;
    if (operation == RAR_TEST) {
      found = true;
      break;
    }
  }
  RARCloseArchive(handle);
  if (!found) {
    ul::SetLastError("That entry could not be read from the archive.");
    return nullptr;
  }
  if (length) *length = captured.size();
  return ul::Duplicate(captured);
}

}  // extern "C"
