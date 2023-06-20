#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "llvm/Support/CommandLine.h"

#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/TypeLocVisitor.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include <iostream>
#include <stdlib.h>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

struct Info {
  std::string Name;
  unsigned Start;
  unsigned End;
};

static std::map<std::string, std::vector<Info> > M;

static llvm::cl::OptionCategory ClangPudgeOptions("clang-pudge option");

static llvm::cl::opt<std::string>
    OutputFile("output-file",
            llvm::cl::desc("JSON file to store output"),
            llvm::cl::value_desc("filename"), llvm::cl::cat(ClangPudgeOptions));

class ClangPudge : public MatchFinder::MatchCallback {
public:

  explicit ClangPudge(const std::vector<std::string>& SourceFiles)
      : SourceFiles(SourceFiles) {}

  void run(const MatchFinder::MatchResult &Result) override {

    SourceManager &SM = Result.Context->getSourceManager();

    std::string FilePath;

    // Handle c/c++ methods
    if (const CXXMethodDecl *MD = Result.Nodes.getNodeAs<CXXMethodDecl>("cxxMethod")) {
      if (MD->hasBody()) {
        FilePath = SM.getFilename(MD->getLocation()).str();
        if (std::find(SourceFiles.begin(), SourceFiles.end(), FilePath) != SourceFiles.end()) {
          Info I;
          I.Name = getMangledName(MD, Result);
          I.Start = SM.getSpellingLineNumber(MD->getBeginLoc());
          I.End = SM.getSpellingLineNumber(MD->getEndLoc());
          M[FilePath].push_back(I);
        }
      }
    } else if (const FunctionDecl *FD = Result.Nodes.getNodeAs<FunctionDecl>("function")) {
      // Handle c/c++ functions
      if (FD->hasBody() && !isMemberFunction(FD)) {
        FilePath = SM.getFilename(FD->getLocation()).str();
        // Check if the file path is in the list of source files
        if (std::find(SourceFiles.begin(), SourceFiles.end(), FilePath) != SourceFiles.end()) {
          Info I;
          I.Name = FD->getNameAsString();
          I.Start = SM.getSpellingLineNumber(FD->getBeginLoc());
          I.End = SM.getSpellingLineNumber(FD->getEndLoc());
          M[FilePath].push_back(I);
        }
      }
    } else if (const ObjCMethodDecl *MD = Result.Nodes.getNodeAs<ObjCMethodDecl>("objcMethod")) {
      // Handle objc methods
      if (MD->hasBody()) {
        FilePath = SM.getFilename(MD->getLocation()).str();
        if (std::find(SourceFiles.begin(), SourceFiles.end(), FilePath) != SourceFiles.end()) {
          Info I;
          I.Name = getMangledName(MD, Result);;
          I.Start = SM.getSpellingLineNumber(MD->getBeginLoc());
          I.End = SM.getSpellingLineNumber(MD->getEndLoc());

          M[FilePath].push_back(I);
        }
      }
    }

  }

  private:
  std::vector<std::string> SourceFiles;

  bool isMemberFunction(const FunctionDecl *Func) {
    return Func->isCXXInstanceMember() || Func->isCXXClassMember();
  }

  std::string getMangledName(const Decl *D, const MatchFinder::MatchResult &Result) {
    auto MC = Result.Context->createMangleContext();
    std::string Name;
    GlobalDecl GD;
    llvm::raw_string_ostream Out(Name);

    if (const auto *MD = dyn_cast<ObjCMethodDecl>(D)) {
      MC->mangleObjCMethodName(MD, Out, false, true);
    } else if (const auto *BD = dyn_cast<BlockDecl>(D)) {
      MC->mangleBlock(D->getDeclContext(), BD, Out);
    } else {
      if (const auto *CtorD = dyn_cast<CXXConstructorDecl>(D)) {
        GD = GlobalDecl(CtorD, Ctor_Complete);
      } else if (const auto *DtorD = dyn_cast<CXXDestructorDecl>(D)) {
        GD = GlobalDecl(DtorD, Dtor_Complete);
      } else if (const auto *ND = dyn_cast<NamedDecl>(D)) {
        if (!MC->shouldMangleDeclName(ND)) {
          if (const auto *FD = dyn_cast<FunctionDecl>(ND)) {
            return FD->getNameInfo().getName().getAsString();
          }
          delete MC;
          return "";
        }
        GD = GlobalDecl(ND);
      } else {
        delete MC;
        return "";
      }

      MC->mangleName(GD, Out);
    }
    Out.flush();

    delete MC;

    return Name;
  }
};

int main(int argc, const char **argv) {
  auto ExpectedParser =
      CommonOptionsParser::create(argc, argv, ClangPudgeOptions);
  if (!ExpectedParser) {
    llvm::errs() << ExpectedParser.takeError();
    return 1;
  }
  CommonOptionsParser &OptionsParser = ExpectedParser.get();
  // tooling::CommonOptionsParser OptionsParser(argc, argv, ClangPudgeOptions);
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  MatchFinder Finder;
  ClangPudge Pudge(OptionsParser.getSourcePathList());
  
  // Matcher for C++ function declarations
  DeclarationMatcher CxxFunctionMatcher = functionDecl(isDefinition()).bind("function");
  Finder.addMatcher(CxxFunctionMatcher, &Pudge);

  // Matcher for C++ method declarations
  DeclarationMatcher CxxMethodMatcher = cxxMethodDecl(isDefinition()).bind("cxxMethod");
  Finder.addMatcher(CxxMethodMatcher, &Pudge);

  // Matcher for Objective-C method declarations
  DeclarationMatcher ObjCMethodMatcher = objcMethodDecl(isDefinition()).bind("objcMethod");
  Finder.addMatcher(ObjCMethodMatcher, &Pudge);
  
  int ret = Tool.run(newFrontendActionFactory(&Finder).get());

  llvm::json::Object JsonObj;

  for (std::map<std::string, std::vector<Info> >::iterator it = M.begin(); it != M.end(); ++it) {
    StringRef FileName = it->first;
    std::vector<Info> InfoList = it->second;
    llvm::json::Array JsonArr;
    for (std::vector<Info>::size_type i = 0; i < InfoList.size(); ++i) {
      Info I = InfoList[i];
      llvm::json::Object Obj;
      Obj["name"] = I.Name;
      Obj["start"] = I.Start;
      Obj["end"] = I.End;
      JsonArr.push_back(std::move(Obj));
    }
    JsonObj[FileName] = std::move(JsonArr);
  }

  llvm::json::Value JsonVal(std::move(JsonObj));

  if (!OutputFile.empty()) {
    std::error_code EC;
    llvm::raw_fd_ostream JsonOut(OutputFile, EC, llvm::sys::fs::OF_None);
    JsonOut << llvm::formatv("{0:2}", JsonVal);
  } else {
    llvm::outs() << llvm::formatv("{0:2}", JsonVal);
  }

  return ret;
}

