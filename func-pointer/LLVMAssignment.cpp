//===- Hello.cpp - Example code from "Writing an LLVM Pass" ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements two versions of the LLVM "Hello World" pass described
// in docs/WritingAnLLVMPass.html
//
//===----------------------------------------------------------------------===//

#include <llvm/Support/CommandLine.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/ToolOutputFile.h>
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include <llvm/IR/Module.h>
#include <llvm/IR/DebugInfoMetadata.h>

#include <llvm/Transforms/Scalar.h>

#include <llvm/IR/Function.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include "llvm/Support/Casting.h"

#include <map>
#include <list>
#include <algorithm>

#if LLVM_VERSION_MAJOR >= 4
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>

#else

#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/IR/IntrinsicInst.h>

#endif
using namespace llvm;
#if LLVM_VERSION_MAJOR >= 4
static ManagedStatic<LLVMContext> GlobalContext;
static LLVMContext &getGlobalContext() { return *GlobalContext; }
#endif
/* In LLVM 5.0, when  -O0 passed to clang , the functions generated with clang will
 * have optnone attribute which would lead to some transform passes disabled, like mem2reg.
 */
#if LLVM_VERSION_MAJOR == 5
struct EnableFunctionOptPass: public FunctionPass {
    static char ID;
    EnableFunctionOptPass():FunctionPass(ID){}
    bool runOnFunction(Function & F) override{
        if(F.hasFnAttribute(Attribute::OptimizeNone))
        {
            F.removeFnAttr(Attribute::OptimizeNone);
        }
        return true;
    }
};

char EnableFunctionOptPass::ID=0;
#endif



///!TODO TO BE COMPLETED BY YOU FOR ASSIGNMENT 2
///Updated 11/10/2017 by fargo: make all functions
///processed by mem2reg before this pass.
struct FuncPtrPass : public ModulePass {
    static char ID; // Pass identification, replacement for typeid
    FuncPtrPass() : ModulePass(ID) {}

    using PossibleFuncPtrList = std::list<Value*>;
    std::map<llvm::CallInst*, PossibleFuncPtrList> callMap{};
    std::map<llvm::PHINode*, PossibleFuncPtrList> phiMap{};
    std::map<llvm::Function*, PossibleFuncPtrList> functionMap{};
    std::map<llvm::Argument*, PossibleFuncPtrList> argMap{};

    bool unionPossibleList(PossibleFuncPtrList &dst, PossibleFuncPtrList &src) {
        auto updated = false;
        for (auto it: src) {
            if (std::find(dst.begin(), dst.end(), it) == dst.end()) {
                dst.push_back(it);
//                errs() << "Insert " << *it << " into dst\n";
                updated = true;
            } else {
//                errs() << *it << " already in dst\n";
            }
        }
        return updated;
    }

    bool unionPossibleList(PossibleFuncPtrList &dst, Value *value) {
        auto updated = false;
        if (CallInst *call_inst = dyn_cast<CallInst>(value)) {
            assert(callMap.find(call_inst) != callMap.end());
            updated = unionPossibleList(dst, callMap[call_inst]) || updated;

        } else if (auto phi_node = dyn_cast<PHINode>(value)) {
            assert(phiMap.find(phi_node) != phiMap.end());
            updated = unionPossibleList(dst, phiMap[phi_node]) || updated;

        } else if (auto arg = dyn_cast<Argument>(value)) {
            assert(argMap.find(arg) != argMap.end());
//            errs() << "Unioning possible list for argument\n";
            updated = unionPossibleList(dst, argMap[arg]) || updated;
        }
        return updated;
    }

    PossibleFuncPtrList getPossibleList(Value *value) {
        if (CallInst *call_inst = dyn_cast<CallInst>(value)) {
            assert(callMap.find(call_inst) != callMap.end());
            return callMap[call_inst];

        } else if (auto phi_node = dyn_cast<PHINode>(value)) {
            assert(phiMap.find(phi_node) != phiMap.end());
            return phiMap[phi_node];

        } else if (auto arg = dyn_cast<Argument>(value)) {
            assert(argMap.find(arg) != argMap.end());
            return argMap[arg];

        } else if (isa<Function>(value)) {
            return PossibleFuncPtrList(1, value);

        } else {
            errs() << *value;
            assert("Unexpected value type!\n" && false);
        }
    }

    void initMap(Module &module) {
        // TODO: find all phi of func ptr and function calls
        for (auto &function: module.getFunctionList()) {
            functionMap[&function] = PossibleFuncPtrList();
            for (auto &arg : function.args()) {
                argMap[&arg] = PossibleFuncPtrList();
            }
            for (auto &bb : function) {
                for (auto &inst : bb) {
                    if (auto callInst = dyn_cast<CallInst>(&inst)) {
                        callMap[callInst] = PossibleFuncPtrList();
                    } else if (auto phi = dyn_cast<PHINode>(&inst)) {
                        phiMap[phi] = PossibleFuncPtrList();
                    }
                }
            }
        }
    }

    template <class T>
    void printMap(T t) {
        for (auto &map_it: t) {
            if (isDebugCall(map_it.first)) {
                continue;
            }
            errs() << *(map_it.first) << " ----------------------------- \n";
            for (auto &list_it: map_it.second) {
                if (list_it->getName() == "") {
                    errs() << "null\n";
                } else {
                    errs() << list_it->getName() << "\n";
                }
            }
        }
    }

    void printMaps() {
//        printMap(functionMap);
//        printMap(callMap);
//        printMap(phiMap);
        printMap(argMap);
    }

    int numIter{};

    bool processStore(StoreInst *storeInst) {
//        errs() << "Process Store Inst\n";
        auto dst = storeInst->getPointerOperand();
        if (!isFunctionPointer(dst->getType())) {
            return false;
        }

        if (auto dst_arg = dyn_cast<Argument>(dst)) {
            PossibleFuncPtrList &possibleList = argMap[dst_arg];
            Value *value = storeInst->getValueOperand();
            if (std::find(possibleList.begin(), possibleList.end(),
                          value) == possibleList.end()) {
                possibleList.push_back(value);
                return true;
            }
        }
        return false;
    }

    bool iterate(Module &module) {
        // TODO: update all phi and function calls
//        errs() << "Iteration " << numIter << "====================\n";
        bool updated = false;
        for (auto &function: module.getFunctionList()) {
            for (auto &bb : function) {
                for (auto &inst : bb) {
//                    errs() << inst << "||||\n";
                    if (auto callInst = dyn_cast<CallInst>(&inst)) {
                        updated = processCall(callInst) || updated;
                    } else if (auto phi = dyn_cast<PHINode>(&inst)) {
                        updated = processPhi(phi) || updated;
                    } else if (auto store = dyn_cast<StoreInst>(&inst)) {
                        updated = processStore(store) || updated;
                    }
                }
            }
            updated = processFunction(&function) || updated;
        }
//        errs() << "End Iteration " << numIter++ << "====================\n";
        return updated;
    }

    template <class T>
    bool isDebugCall(T callInst) {
        return isa<DbgValueInst>(callInst);
    }

    StringRef funcNameWrapper(Function *function) {
        if (!function || function->getName() == "") {
            return "NULL";

        } else {
            return function->getName();
        }
    }

    void printCalls(Module &module) {
        for (auto &function: module.getFunctionList()) {
            for (auto &bb : function) {
                for (auto &inst : bb) {
                    if (auto callInst = dyn_cast<CallInst>(&inst)) {
                        if (isDebugCall(callInst)) {
                            continue;
                        }
                        auto calledValue = callInst->getCalledValue();
                        bool isCallingValue = calledValue != nullptr;
                        std::list<Value *> &&possible_func_list =
                                isCallingValue ? std::move(getPossibleList(calledValue)) :
                                PossibleFuncPtrList(1, callInst->getCalledFunction());
                        MDNode *metadata = callInst->getMetadata(0);
                        if (!metadata) {
                            errs() << "No meta data found for " << *callInst;
                            continue;
                        }
                        DILocation *debugLocation = dyn_cast<DILocation>(metadata);
                        if (!debugLocation) {
                            errs() << "No debug location found for " << *callInst;
                            continue;
                        }
                        errs() << debugLocation->getLine() << " : ";
                        bool first = true;
                        for (auto value : possible_func_list) {
                            auto func = dyn_cast<Function>(value);
//                            assert(func);
                            if (!func|| func->getName() == "") {
                                continue;
                            }
                            if (!first) {
                                errs() << ", ";
                            } else {
                                first = false;
                            }
                            errs() << func->getName();
                        }
                        errs() << "\n";
                    }
                }
            }
        }
    }

    bool processCall(CallInst *callInst) {
        // TODO: for call, add all phied possible list to possible list
        // If parameter is func ptr, add corresponding possible list to arg's possible list

        if (isDebugCall(callInst)) {
            // ignore these calls
            return false;
        }
        bool updated = false;
        auto calledValue = callInst->getCalledValue();

        bool isCallingValue = calledValue != nullptr;

        // TODO: function pointer consumption
        std::list<Value *> &&possible_func_list =
                isCallingValue ? std::move(getPossibleList(calledValue)) :
                PossibleFuncPtrList(1, callInst->getCalledFunction());

        // TODO: function pointer arg

//        errs() << "CallInst:" << *callInst << " ----------------------\n";
        for (auto value : possible_func_list) {
//            errs() << "Called value:" << *value << "\n";
            Function *func = dyn_cast<Function>(value);
            if (!func) {
//                errs() << "null\n";
                continue;
            } else {
//                errs() << func->getName() << "\n";
            }
            unsigned i = 0;
            for (auto &arg : func->args()) {
                if (!isFunctionPointer(arg.getType())) {
                    i++;
                    continue;
                }
//                    errs() << "pass possible list for argument\n";
                auto arg_in = callInst->getArgOperand(i);
//                    errs() << i << "th Arg:" << arg.getName() << "<---" << arg_in->getName() << "\n";
                if (isa<Function>(arg_in)) {
                    PossibleFuncPtrList &possibleList = argMap[&arg];
                    if (std::find(possibleList.begin(), possibleList.end(),
                                  arg_in) == possibleList.end()) {
                        possibleList.push_back(arg_in);
                        updated = true;
                    }

                } else {
                    updated = unionPossibleList(argMap[&arg], arg_in) || updated;
                }
                i++;
            }
        }

        // TODO: function pointer return value
        for (auto value : possible_func_list) {
            Function *func = dyn_cast<Function>(value);
            if (!func || func->getName() == "") {
                continue;
            }
//            errs() << func->getName() << "\n";
            updated = unionPossibleList(callMap[callInst], functionMap[func]) || updated;
        }

        return updated;


#ifdef NEVER_DEFINED
        errs() << "CallInst:" << *callInst << " ----------------------\n";
        for (auto value : possible_func_list) {
            Function *func = dyn_cast<Function>(value);
            assert(func);
            if (func->getName() == "") {
                errs() << "null\n";
            } else {
                errs() << func->getName() << "\n";
            }
        }
#endif
    }

    bool processPhi(PHINode *phiNode) {
        // TODO: for phi, add all phied possible list to possible list
//        errs() << "phi is called\n";
        if (!isFunctionPointer(phiNode->getType())) {
//            errs() << "Phi Node is not function pointer\n";
            return false;
        }

        bool updated = false;
        PossibleFuncPtrList &possible_list = phiMap[phiNode];

        for (unsigned i = 0, e = phiNode->getNumIncomingValues();
                i != e; i++) {
            auto incomingValue = phiNode->getIncomingValue(i);
            assert(phiMap.find(phiNode) != phiMap.end());

            // add phied values into possible list

            if (!isa<PHINode>(incomingValue) &&
                    std::find(possible_list.begin(), possible_list.end(),
                              incomingValue) == possible_list.end()) {
                possible_list.push_back(incomingValue);
                updated = true;
//                    errs() << "Value" << i << "  ------------------\n";
//                    errs() << *incomingValue << "\n";
            }

            // add possible list of phied values into possible list
            updated = unionPossibleList(possible_list, incomingValue) || updated;

        }
//        if (updated) {
//            errs() << "Something updated for Phi Node\n";
//        } else {
//            errs() << "Nothing updated for Phi Node\n";
//        }
        return updated;
    }

    bool isFunctionPointer(Type *type) {
        if (!type->isPointerTy()) {
            return false;
        }
        auto pointee = type->getPointerElementType();
        return pointee->isFunctionTy();
    }

    bool processFunction(Function *function) {
        // TODO: for functions, if return value is function ptr,
        // add their possible values to functionMap

        auto returnType = function->getReturnType();
        if (!isFunctionPointer(returnType)) {
            return false;
        }

        bool updated = false;
        assert(functionMap.find(function) != functionMap.end());
        for (auto &bb : *function) {
            for (auto &inst : bb) {
                if (auto returnInst = dyn_cast<ReturnInst>(&inst)) {
                    auto value = returnInst->getReturnValue();
                    // 函数总是后序遍历的，因此这里假设这些value的possible list 已经有了
                    updated = unionPossibleList(functionMap[function], value) || updated;
                }
            }
        }
//        if (!updated) {
//            errs() << "Nothing updated for Function\n";
//        }
        return updated;
    }

    bool runOnModule(Module &module) override {
        errs().write_escaped(module.getName()) << '\n';

        initMap(module);

//        printMaps();

        while (iterate(module));

        printCalls(module);

//        printMaps();
#ifdef NEVER_DEFINED
        for (auto &function: module.getFunctionList()) {
            for (auto &bb : function) {
                for (auto &inst : bb) {
                    errs() << inst.getName() << "||: " << inst.getOpcodeName() << "\n";
                    if (auto callInst = dyn_cast<CallInst>(&inst)) {
                        errs() << "Call Inst: =================================\n";
                        errs() << inst << "\n";
                        if (DbgValueInst *dbgValueInst = dyn_cast<DbgValueInst>(&inst)) {
                            errs() << *dbgValueInst << "\n";
                        } else {
                            errs() << "Not dbg declare\n";
                        }
                        if (Function *calledFunction = callInst->getCalledFunction()) {
//                            errs() << "Called function: ---------------------------";
//                            errs() << *calledFunction << "\n";

                        } else {
                            errs() << "Called function not found\n";
                            errs() << "Called Value: " << *(callInst->getCalledValue()) << "\n";
                        }
                    } else if (auto binOp = dyn_cast<BinaryOperator>(&inst)) {
                        errs() << "Binary Operation: =================================\n";
                        errs() << *binOp << "\n";
                    } else if (auto load = dyn_cast<LoadInst>(&inst)) {
                        errs() << "Load: =================================\n";
                        errs() << *load << "\n";
                    }
                }
            }
        }
#endif

//        errs() << "------------------------------\n";
        return false;
    }
};


char FuncPtrPass::ID = 0;
static RegisterPass<FuncPtrPass> X("funcptrpass", "Print function call instruction");

static cl::opt<std::string>
        InputFilename(cl::Positional,
                      cl::desc("<filename>.bc"),
                      cl::init(""));


int main(int argc, char **argv) {
    LLVMContext &Context = getGlobalContext();
    SMDiagnostic Err;
    // Parse the command line to read the Inputfilename
    cl::ParseCommandLineOptions(argc, argv,
                                "FuncPtrPass \n My first LLVM too which does not do much.\n");


    // Load the input module
    std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, Context);
    if (!M) {
        Err.print(argv[0], errs());
        return 1;
    }

    llvm::legacy::PassManager Passes;

    ///Remove functions' optnone attribute in LLVM5.0
#if LLVM_VERSION_MAJOR == 5
    Passes.add(new EnableFunctionOptPass());
#endif
    ///Transform it to SSA
    Passes.add(llvm::createPromoteMemoryToRegisterPass());

    /// Your pass to print Function and Call Instructions
    Passes.add(new FuncPtrPass());
    Passes.run(*M.get());
}

