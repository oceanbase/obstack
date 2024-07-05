//===-- llvm-dwarfdump.cpp - Debug info dumping utility for llvm ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This program is a utility that works like "dwarfdump".
//
//===----------------------------------------------------------------------===//

#include "llvmtool/llvm-dwarfdump.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Triple.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"
#include <setjmp.h>
#include "lib/signal.h"
#include "common/log.h"
#include "utils/defer.h"

namespace _obstack
{
using namespace llvm;
using namespace object;

sigjmp_buf jmp;
static void fault_tolerant_handler(int)
{
  siglongjmp(jmp, 1);
}

namespace {
static unsigned DumpType = DIDT_Null;

} // namespace
/// @}
//===----------------------------------------------------------------------===//

static void error(StringRef Prefix, std::error_code EC) {
  if (!EC)
    return;
  errs() << Prefix << ": " << EC.message() << "\n";
  exit(1);
}

struct FuncData
{
  std::vector<ulong> *addrs_;
  std::vector<_obstack::LineInfo> *line_infos_;
};

using HandlerFn = std::function<bool(ObjectFile &, DWARFContext &DICtx, Twine,
                                     raw_ostream &, void *)>;

static bool lookup(DWARFContext &DICtx, uint64_t Address, raw_ostream &OS, _obstack::LineInfo *line_info) {
  auto DIEsForAddr = DICtx.getDIEsForAddress(Address);

  if (!DIEsForAddr)
    return false;

  object::SectionedAddress saddress;
  saddress.Address = Address;

  DILineInfoSpecifier dis{.FLIKind = DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath, .FNKind = DINameKind::None};
  if (DILineInfo LineInfo = DICtx.getLineInfoForAddress(saddress, dis)) {
    line_info->filename_ = LineInfo.FileName;
    line_info->line_ = LineInfo.Line;
    return true;
  }

  return false;
}

static bool dumpObjectFile(ObjectFile &Obj, DWARFContext &DICtx, Twine Filename,
                           raw_ostream &OS, void *arg) {
  logAllUnhandledErrors(DICtx.loadRegisterInfo(Obj), errs(),
                        Filename.str() + ": ");
  auto &addrs = *((FuncData*)arg)->addrs_;
  auto &line_infos = *((FuncData*)arg)->line_infos_;
  auto handler_bak = lib::tl_signal_handler;
  DEFER(lib::tl_signal_handler = handler_bak);
  for (int i = 0; i < addrs.size(); i++) {
    int js = sigsetjmp(jmp, 1);
    if (0 == js) {
      lib::tl_signal_handler = fault_tolerant_handler;
      lookup(DICtx, addrs[i], OS, &line_infos[i]);
    } else if (1 == js) {
      LOG(DEBUG, "llvm lookup failed, address: %lu", addrs[i]);
    } else {
      LOG(ERROR, "unexpected error, address: %lu", addrs[i]);
    }
  }
  return true;
}

static bool handleBuffer(StringRef Filename, MemoryBufferRef Buffer,
                         HandlerFn HandleObj, raw_ostream &OS, void *arg);

static bool handleArchive(StringRef Filename, Archive &Arch,
                          HandlerFn HandleObj, raw_ostream &OS, void *arg) {
  bool Result = true;
  Error Err = Error::success();
  for (auto Child : Arch.children(Err)) {
    auto BuffOrErr = Child.getMemoryBufferRef();
    error(Filename, errorToErrorCode(BuffOrErr.takeError()));
    auto NameOrErr = Child.getName();
    error(Filename, errorToErrorCode(NameOrErr.takeError()));
    std::string Name = (Filename + "(" + NameOrErr.get() + ")").str();
    Result &= handleBuffer(Name, BuffOrErr.get(), HandleObj, OS, arg);
  }
  error(Filename, errorToErrorCode(std::move(Err)));

  return Result;
}

static bool handleBuffer(StringRef Filename, MemoryBufferRef Buffer,
                         HandlerFn HandleObj, raw_ostream &OS, void *arg) {
  Expected<std::unique_ptr<Binary>> BinOrErr = object::createBinary(Buffer);
  error(Filename, errorToErrorCode(BinOrErr.takeError()));

  bool Result = true;
  if (auto *Obj = dyn_cast<ObjectFile>(BinOrErr->get())) {
    std::unique_ptr<DWARFContext> DICtx = DWARFContext::create(*Obj);
    Result = HandleObj(*Obj, *DICtx, Filename, OS, arg);
  }
  else if (auto *Fat = dyn_cast<MachOUniversalBinary>(BinOrErr->get()))
    for (auto &ObjForArch : Fat->objects()) {
      std::string ObjName =
          (Filename + "(" + ObjForArch.getArchFlagName() + ")").str();
      if (auto MachOOrErr = ObjForArch.getAsObjectFile()) {
        auto &Obj = **MachOOrErr;
        std::unique_ptr<DWARFContext> DICtx = DWARFContext::create(Obj);
        Result &= HandleObj(Obj, *DICtx, ObjName, OS, arg);
        continue;
      } else
        consumeError(MachOOrErr.takeError());
      if (auto ArchiveOrErr = ObjForArch.getAsArchive()) {
        error(ObjName, errorToErrorCode(ArchiveOrErr.takeError()));
        Result &= handleArchive(ObjName, *ArchiveOrErr.get(), HandleObj, OS, arg);
        continue;
      } else
        consumeError(ArchiveOrErr.takeError());
    }
  else if (auto *Arch = dyn_cast<Archive>(BinOrErr->get()))
    Result = handleArchive(Filename, *Arch, HandleObj, OS, arg);
  return Result;
}

static bool handleFile(StringRef Filename, HandlerFn HandleObj,
                       raw_ostream &OS, void *arg) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> BuffOrErr =
  MemoryBuffer::getFileOrSTDIN(Filename);
  error(Filename, BuffOrErr.getError());
  std::unique_ptr<MemoryBuffer> Buffer = std::move(BuffOrErr.get());
  return handleBuffer(Filename, *Buffer, HandleObj, OS, arg);
}

/// If the input path is a .dSYM bundle (as created by the dsymutil tool),
/// replace it with individual entries for each of the object files inside the
/// bundle otherwise return the input path.
static std::vector<std::string> expandBundle(const std::string &InputPath) {
  std::vector<std::string> BundlePaths;
  SmallString<256> BundlePath(InputPath);
  // Manually open up the bundle to avoid introducing additional dependencies.
  if (sys::fs::is_directory(BundlePath) &&
      sys::path::extension(BundlePath) == ".dSYM") {
    std::error_code EC;
    sys::path::append(BundlePath, "Contents", "Resources", "DWARF");
    for (sys::fs::directory_iterator Dir(BundlePath, EC), DirEnd;
         Dir != DirEnd && !EC; Dir.increment(EC)) {
      const std::string &Path = Dir->path();
      sys::fs::file_status Status;
      EC = sys::fs::status(Path, Status);
      error(Path, EC);
      switch (Status.type()) {
      case sys::fs::file_type::regular_file:
      case sys::fs::file_type::symlink_file:
      case sys::fs::file_type::type_unknown:
        BundlePaths.push_back(Path);
        break;
      default: /*ignore*/;
      }
    }
    error(BundlePath, EC);
  }
  if (!BundlePaths.size())
    BundlePaths.push_back(InputPath);
  return BundlePaths;
}

LLVMDwarfDump::LLVMDwarfDump(const char *file)
{
  objs_ = expandBundle(file);
}

void LLVMDwarfDump::addr2line(std::vector<ulong> &addrs, std::vector<_obstack::LineInfo> &line_infos) {
  // used for disable waning log of "Unable to find target for this triple"
  llvm::InitializeAllTargetInfos();
  if (addrs.size() != line_infos.size()) return;
  raw_ostream &OS = outs();
  DumpType = DIDT_DebugInfo;

  // Expand any .dSYM bundles to the individual object files contained therein.
  for (auto object : objs_) {
    FuncData data{.addrs_ = &addrs, .line_infos_ = &line_infos};
    handleFile(object, dumpObjectFile, OS, &data);
  }
}

}
