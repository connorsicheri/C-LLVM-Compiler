#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <set>
#include <map>
#include <vector>

using namespace llvm;

namespace {
    struct Alloca2RegPass : public FunctionPass {
        static char ID;
        Alloca2RegPass() : FunctionPass(ID) {}

        std::set<AllocaInst*> TargetAllocas;
        std::map<BasicBlock*, std::map<AllocaInst*, Value*>> Post;
        std::map<BasicBlock*, std::map<AllocaInst*, PHINode*>> Pre;

        // Step 1: Find all scalar integer allocas that are only used by
        // load/store through the pointer (safe to promote to registers).
        void collectTargetAllocas(Function &F) {
            TargetAllocas.clear();
            // Scan ALL basic blocks — our compiler can place allocas inside
            // loop bodies (e.g. variables declared inside a for-loop scope).
            for (BasicBlock &BB : F) {
                for (Instruction &I : BB) {
                    AllocaInst *AI = dyn_cast<AllocaInst>(&I);
                    if (!AI) continue;

                    // Only promote scalar integer types (i32, i1, i8)
                    Type *allocatedType = AI->getAllocatedType();
                    if (!allocatedType->isIntegerTy()) continue;

                    // Every use must be a load or store with this alloca as
                    // the pointer operand. Reject if address escapes.
                    bool safe = true;
                    for (User *U : AI->users()) {
                        if (LoadInst *LI = dyn_cast<LoadInst>(U)) {
                            if (LI->getPointerOperand() != AI)
                                { safe = false; break; }
                        } else if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
                            if (SI->getPointerOperand() != AI)
                                { safe = false; break; }
                            // Reject if the alloca address itself is stored
                            if (SI->getValueOperand() == AI)
                                { safe = false; break; }
                        } else {
                            safe = false; break;
                        }
                    }
                    if (safe)
                        TargetAllocas.insert(AI);
                }
            }
        }

        virtual bool runOnFunction(Function &F) {
            errs() << "Working on function called " << F.getName() << "!\n";
            collectTargetAllocas(F);
            if (TargetAllocas.empty())
                return false;

            Post.clear();
            Pre.clear();

            BasicBlock &entryBB = F.getEntryBlock();

            // ---- Step 2: Create phi nodes at top of every non-entry BB ----
            // We create phis eagerly; unused ones are cleaned up later.
            for (AllocaInst *AI : TargetAllocas) {
                Type *allocatedType = AI->getAllocatedType();
                for (BasicBlock &BB : F) {
                    if (&BB == &entryBB) continue;
                    unsigned numPreds = std::distance(pred_begin(&BB), pred_end(&BB));
                    if (numPreds > 0) {
                        PHINode *phi = PHINode::Create(
                            allocatedType, numPreds, "", &BB.front());
                        Pre[&BB][AI] = phi;
                    }
                }
            }

            // ---- Step 3: Walk each BB, replace loads, track Post values ----
            for (AllocaInst *AI : TargetAllocas) {
                Type *allocatedType = AI->getAllocatedType();
                for (BasicBlock &BB : F) {
                    // Initial "current" value of this variable entering the block:
                    //  - entry block: null (becomes undef if read before write)
                    //  - other blocks: the phi we created in step 2
                    Value *current = nullptr;
                    if (&BB != &entryBB && Pre.count(&BB) && Pre[&BB].count(AI))
                        current = Pre[&BB][AI];

                    for (Instruction &I : BB) {
                        if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
                            if (SI->getPointerOperand() == AI)
                                current = SI->getValueOperand();
                        } else if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
                            if (LI->getPointerOperand() == AI) {
                                if (!current)
                                    current = UndefValue::get(allocatedType);
                                LI->replaceAllUsesWith(current);
                            }
                        }
                    }

                    // Record the value of this variable at the end of BB
                    Post[&BB][AI] = current ? current
                                            : UndefValue::get(allocatedType);
                }
            }

            // ---- Step 4: Fill phi incoming edges from predecessor Post ----
            for (auto &bbEntry : Pre) {
                BasicBlock *BB = bbEntry.first;
                for (auto &aiEntry : bbEntry.second) {
                    AllocaInst *AI = aiEntry.first;
                    PHINode *phi = aiEntry.second;
                    for (BasicBlock *Pred : predecessors(BB)) {
                        Value *incoming = UndefValue::get(AI->getAllocatedType());
                        if (Post.count(Pred) && Post[Pred].count(AI))
                            incoming = Post[Pred][AI];
                        phi->addIncoming(incoming, Pred);
                    }
                }
            }

            // ---- Step 5: Delete dead loads and stores for promoted allocas ----
            std::vector<Instruction*> toRemove;
            for (BasicBlock &BB : F) {
                for (Instruction &I : BB) {
                    if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
                        if (AllocaInst *AI = dyn_cast<AllocaInst>(SI->getPointerOperand()))
                            if (TargetAllocas.count(AI))
                                toRemove.push_back(SI);
                    } else if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
                        if (AllocaInst *AI = dyn_cast<AllocaInst>(LI->getPointerOperand()))
                            if (TargetAllocas.count(AI))
                                toRemove.push_back(LI);
                    }
                }
            }
            for (Instruction *I : toRemove)
                I->eraseFromParent();

            // ---- Step 6: Delete the alloca instructions themselves ----
            for (AllocaInst *AI : TargetAllocas)
                AI->eraseFromParent();

            // ---- Step 7: Clean up unused and redundant phi nodes ----
            // Repeat until no more simplifications can be made.
            bool changed = true;
            while (changed) {
                changed = false;
                for (BasicBlock &BB : F) {
                    auto it = BB.begin();
                    while (it != BB.end()) {
                        PHINode *phi = dyn_cast<PHINode>(&*it);
                        if (!phi) break;  // phis are always at block start
                        ++it;  // advance before potential erase

                        // Remove unused phis
                        if (phi->use_empty()) {
                            phi->eraseFromParent();
                            changed = true;
                            continue;
                        }

                        // Fold trivial phis: all incoming values are the
                        // same (ignoring self-references)
                        Value *common = nullptr;
                        bool trivial = true;
                        for (unsigned i = 0; i < phi->getNumIncomingValues(); i++) {
                            Value *v = phi->getIncomingValue(i);
                            if (v == phi) continue;  // self-reference
                            if (!common)
                                common = v;
                            else if (v != common)
                                { trivial = false; break; }
                        }
                        if (trivial) {
                            Value *replacement = common
                                ? common
                                : UndefValue::get(phi->getType());
                            phi->replaceAllUsesWith(replacement);
                            phi->eraseFromParent();
                            changed = true;
                        }
                    }
                }
            }

            // ========== A7 OPTIMIZATION: Function Inlining ==========
            inlineCalls(F);

            // ========== A7 OPTIMIZATIONS (iterated to fixpoint) ==========
            bool optChanged = true;
            while (optChanged) {
                optChanged = false;
                optChanged |= constantFold(F);
                optChanged |= algebraicSimplify(F);
                optChanged |= localCSE(F);
                optChanged |= deadCodeElim(F);
                optChanged |= loopInvariantCodeMotion(F);
            }

            return true;
        }

        // Constant Folding: evaluate instructions with all-constant operands
        bool constantFold(Function &F) {
            bool everChanged = false;
            bool changed = true;
            while (changed) {
                changed = false;
                for (BasicBlock &BB : F) {
                    for (auto it = BB.begin(); it != BB.end(); ) {
                        Instruction *I = &*it++;

                        if (BinaryOperator *BO = dyn_cast<BinaryOperator>(I)) {
                            ConstantInt *L = dyn_cast<ConstantInt>(BO->getOperand(0));
                            ConstantInt *R = dyn_cast<ConstantInt>(BO->getOperand(1));
                            if (!L || !R) continue;
                            int64_t lv = L->getSExtValue(), rv = R->getSExtValue();
                            int64_t result;
                            switch (BO->getOpcode()) {
                                case Instruction::Add:  result = lv + rv; break;
                                case Instruction::Sub:  result = lv - rv; break;
                                case Instruction::Mul:  result = lv * rv; break;
                                case Instruction::SDiv:
                                    if (rv == 0) continue;
                                    result = lv / rv; break;
                                default: continue;
                            }
                            Value *C = ConstantInt::get(BO->getType(), result, true);
                            BO->replaceAllUsesWith(C);
                            BO->eraseFromParent();
                            changed = true; everChanged = true;
                        } else if (ICmpInst *IC = dyn_cast<ICmpInst>(I)) {
                            ConstantInt *L = dyn_cast<ConstantInt>(IC->getOperand(0));
                            ConstantInt *R = dyn_cast<ConstantInt>(IC->getOperand(1));
                            if (!L || !R) continue;
                            int64_t lv = L->getSExtValue(), rv = R->getSExtValue();
                            bool result;
                            switch (IC->getPredicate()) {
                                case ICmpInst::ICMP_EQ:  result = (lv == rv); break;
                                case ICmpInst::ICMP_NE:  result = (lv != rv); break;
                                case ICmpInst::ICMP_SLT: result = (lv < rv); break;
                                case ICmpInst::ICMP_SLE: result = (lv <= rv); break;
                                case ICmpInst::ICMP_SGT: result = (lv > rv); break;
                                case ICmpInst::ICMP_SGE: result = (lv >= rv); break;
                                default: continue;
                            }
                            Value *C = ConstantInt::get(
                                Type::getInt1Ty(F.getContext()), result ? 1 : 0);
                            IC->replaceAllUsesWith(C);
                            IC->eraseFromParent();
                            changed = true; everChanged = true;
                        }
                    }
                }
            }
            return everChanged;
        }

        // Algebraic Simplification: X+0->X, X*1->X, X*0->0, etc.
        bool algebraicSimplify(Function &F) {
            bool everChanged = false;
            bool changed = true;
            while (changed) {
                changed = false;
                for (BasicBlock &BB : F) {
                    for (auto it = BB.begin(); it != BB.end(); ) {
                        Instruction *I = &*it++;
                        BinaryOperator *BO = dyn_cast<BinaryOperator>(I);
                        if (!BO) continue;

                        Value *L = BO->getOperand(0);
                        Value *R = BO->getOperand(1);
                        ConstantInt *CL = dyn_cast<ConstantInt>(L);
                        ConstantInt *CR = dyn_cast<ConstantInt>(R);
                        Value *replacement = nullptr;

                        switch (BO->getOpcode()) {
                            case Instruction::Add:
                                if (CR && CR->isZero()) replacement = L;
                                else if (CL && CL->isZero()) replacement = R;
                                break;
                            case Instruction::Sub:
                                if (CR && CR->isZero()) replacement = L;
                                else if (L == R)
                                    replacement = ConstantInt::get(BO->getType(), 0);
                                break;
                            case Instruction::Mul:
                                if ((CR && CR->isZero()) || (CL && CL->isZero()))
                                    replacement = ConstantInt::get(BO->getType(), 0);
                                else if (CR && CR->isOne()) replacement = L;
                                else if (CL && CL->isOne()) replacement = R;
                                break;
                            case Instruction::SDiv:
                                if (CR && CR->isOne()) replacement = L;
                                break;
                            default: break;
                        }

                        if (replacement) {
                            BO->replaceAllUsesWith(replacement);
                            BO->eraseFromParent();
                            changed = true; everChanged = true;
                        }
                    }
                }
            }
            return everChanged;
        }

        // Local CSE: within each basic block, if we see the same binary op
        // or icmp with the same operands, reuse the earlier result.
        // Also CSE loads from the same pointer within a block (invalidated by stores).
        bool localCSE(Function &F) {
            bool everChanged = false;
            bool changed = true;
            while (changed) {
                changed = false;
                for (BasicBlock &BB : F) {
                    // Map: (opcode, op0, op1) -> first instruction computing it
                    std::map<std::tuple<unsigned, Value*, Value*>, Instruction*> seen;
                    // Map: pointer -> last load from that pointer (invalidated by stores)
                    std::map<Value*, LoadInst*> loadMap;

                    for (auto it = BB.begin(); it != BB.end(); ) {
                        Instruction *I = &*it++;

                        // CSE for binary ops and icmp
                        if (isa<BinaryOperator>(I) || isa<ICmpInst>(I)) {
                            unsigned opcode = I->getOpcode();
                            Value *op0 = I->getOperand(0);
                            Value *op1 = I->getOperand(1);
                            // For icmp, encode the predicate in the key
                            if (ICmpInst *IC = dyn_cast<ICmpInst>(I))
                                opcode = (opcode << 16) | IC->getPredicate();

                            auto key = std::make_tuple(opcode, op0, op1);
                            auto found = seen.find(key);
                            if (found != seen.end()) {
                                I->replaceAllUsesWith(found->second);
                                I->eraseFromParent();
                                changed = true; everChanged = true;
                            } else {
                                seen[key] = I;
                                // Also check commutative ops
                                if (isa<BinaryOperator>(I)) {
                                    BinaryOperator *BO = cast<BinaryOperator>(I);
                                    if (BO->isCommutative()) {
                                        auto rkey = std::make_tuple(opcode, op1, op0);
                                        if (seen.find(rkey) == seen.end())
                                            seen[rkey] = I;
                                    }
                                }
                            }
                            continue;
                        }

                        // CSE for loads: reuse if same pointer, no intervening store
                        if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
                            Value *ptr = LI->getPointerOperand();
                            auto found = loadMap.find(ptr);
                            if (found != loadMap.end()) {
                                LI->replaceAllUsesWith(found->second);
                                LI->eraseFromParent();
                                changed = true; everChanged = true;
                            } else {
                                loadMap[ptr] = LI;
                            }
                            continue;
                        }

                        // A store or call invalidates load CSE
                        if (isa<StoreInst>(I) || isa<CallInst>(I)) {
                            loadMap.clear();
                        }
                    }
                }
            }
            return everChanged;
        }

        // Dead Code Elimination: remove instructions with no uses and no
        // side effects (not stores, calls, terminators, etc.)
        bool deadCodeElim(Function &F) {
            bool everChanged = false;
            bool changed = true;
            while (changed) {
                changed = false;
                for (BasicBlock &BB : F) {
                    for (auto it = BB.begin(); it != BB.end(); ) {
                        Instruction *I = &*it++;
                        if (I->use_empty() && !I->isTerminator()
                            && !I->mayHaveSideEffects()) {
                            I->eraseFromParent();
                            changed = true; everChanged = true;
                        }
                    }
                }
            }
            return everChanged;
        }

        // Helper: find all blocks in a natural loop given a header and
        // back-edge source. Uses reverse reachability from latch to header.
        std::set<BasicBlock*> findLoopBlocks(BasicBlock *header, BasicBlock *latch) {
            std::set<BasicBlock*> loop;
            loop.insert(header);
            if (header == latch) return loop;
            // Work backwards from latch
            std::vector<BasicBlock*> worklist;
            loop.insert(latch);
            worklist.push_back(latch);
            while (!worklist.empty()) {
                BasicBlock *BB = worklist.back();
                worklist.pop_back();
                for (BasicBlock *Pred : predecessors(BB)) {
                    if (loop.insert(Pred).second)
                        worklist.push_back(Pred);
                }
            }
            return loop;
        }

        // LICM: hoist loop-invariant loads of global variables out of loops.
        // Detect natural loops via back-edges, check if any store in the
        // loop may alias the global, and if not, hoist the load to the
        // loop preheader.
        bool loopInvariantCodeMotion(Function &F) {
            bool everChanged = false;

            // Find back-edges: edge (B -> H) where H dominates B.
            // Simple dominance: H dominates B if every path from entry to
            // B goes through H. We use a simple BFS-based dominator check.
            // For our structured CFG this works fine.
            BasicBlock &entry = F.getEntryBlock();

            // Compute dominators using simple iterative dataflow
            std::map<BasicBlock*, std::set<BasicBlock*>> doms;
            std::vector<BasicBlock*> allBlocks;
            for (BasicBlock &BB : F) allBlocks.push_back(&BB);

            // Initialize: entry dominated only by itself, others by all
            for (BasicBlock *BB : allBlocks) {
                if (BB == &entry)
                    doms[BB].insert(BB);
                else
                    doms[BB] = std::set<BasicBlock*>(allBlocks.begin(), allBlocks.end());
            }

            bool domChanged = true;
            while (domChanged) {
                domChanged = false;
                for (BasicBlock *BB : allBlocks) {
                    if (BB == &entry) continue;
                    std::set<BasicBlock*> newDom;
                    bool first = true;
                    for (BasicBlock *Pred : predecessors(BB)) {
                        if (first) {
                            newDom = doms[Pred];
                            first = false;
                        } else {
                            std::set<BasicBlock*> inter;
                            for (BasicBlock *D : newDom)
                                if (doms[Pred].count(D)) inter.insert(D);
                            newDom = inter;
                        }
                    }
                    newDom.insert(BB);
                    if (newDom != doms[BB]) {
                        doms[BB] = newDom;
                        domChanged = true;
                    }
                }
            }

            // Find back-edges and process each natural loop
            for (BasicBlock *BB : allBlocks) {
                for (BasicBlock *Succ : successors(BB)) {
                    // Back-edge: Succ dominates BB
                    if (!doms[BB].count(Succ)) continue;

                    BasicBlock *header = Succ;
                    BasicBlock *latch = BB;
                    std::set<BasicBlock*> loopBlocks = findLoopBlocks(header, latch);

                    // Find the preheader: a predecessor of header not in the loop
                    BasicBlock *preheader = nullptr;
                    for (BasicBlock *Pred : predecessors(header)) {
                        if (!loopBlocks.count(Pred)) {
                            preheader = Pred;
                            break;
                        }
                    }
                    if (!preheader) continue;

                    // Collect globals stored to inside the loop
                    std::set<Value*> storedGlobals;
                    bool hasUnknownStore = false;
                    for (BasicBlock *LB : loopBlocks) {
                        for (Instruction &I : *LB) {
                            if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
                                Value *ptr = SI->getPointerOperand();
                                if (isa<GlobalVariable>(ptr))
                                    storedGlobals.insert(ptr);
                                else
                                    // Store through non-global pointer — could alias anything
                                    // but for MiniC we know GEP into global arrays is fine
                                    // (it doesn't alias a scalar global)
                                    ;
                            }
                            if (CallInst *CI = dyn_cast<CallInst>(&I)) {
                                // Calls may modify globals — be conservative
                                // but for known I/O functions, they don't modify our globals
                                Function *callee = CI->getCalledFunction();
                                if (callee) {
                                    std::string name = callee->getName().str();
                                    if (name != "putint" && name != "putnewline"
                                        && name != "putcharacter" && name != "getint") {
                                        hasUnknownStore = true;
                                    }
                                } else {
                                    hasUnknownStore = true;
                                }
                            }
                        }
                    }

                    if (hasUnknownStore) continue;

                    // Hoist loads of globals that aren't stored to
                    // We hoist from the header block first (most impactful)
                    // then from inner blocks too
                    for (BasicBlock *LB : loopBlocks) {
                        for (auto it = LB->begin(); it != LB->end(); ) {
                            Instruction *I = &*it++;
                            LoadInst *LI = dyn_cast<LoadInst>(I);
                            if (!LI) continue;

                            Value *ptr = LI->getPointerOperand();
                            if (!isa<GlobalVariable>(ptr)) continue;
                            if (storedGlobals.count(ptr)) continue;

                            // Safe to hoist! Move to end of preheader
                            // (before its terminator)
                            LI->moveBefore(preheader->getTerminator());
                            everChanged = true;
                        }
                    }
                }
            }
            return everChanged;
        }

        // Function Inlining: inline small non-recursive functions at their
        // call sites. This exposes callee code to LICM, CSE, constant
        // propagation, etc.
        bool inlineCalls(Function &F) {
            bool everChanged = false;

            // Collect call sites first (avoid modifying while iterating)
            bool inlined = true;
            while (inlined) {
                inlined = false;
                std::vector<CallInst*> calls;
                for (BasicBlock &BB : F) {
                    for (Instruction &I : BB) {
                        CallInst *CI = dyn_cast<CallInst>(&I);
                        if (!CI) continue;
                        Function *callee = CI->getCalledFunction();
                        if (!callee || callee->isDeclaration()) continue;
                        // Don't inline recursive calls
                        if (callee == &F) continue;
                        // Don't inline huge functions (> 100 instructions)
                        unsigned instCount = 0;
                        for (BasicBlock &CBB : *callee)
                            instCount += CBB.size();
                        if (instCount > 100) continue;
                        // Don't inline if callee calls itself (recursive)
                        bool selfRecursive = false;
                        for (BasicBlock &CBB : *callee)
                            for (Instruction &CI2 : CBB)
                                if (CallInst *inner = dyn_cast<CallInst>(&CI2))
                                    if (inner->getCalledFunction() == callee)
                                        selfRecursive = true;
                        if (selfRecursive) continue;
                        calls.push_back(CI);
                    }
                }

                for (CallInst *CI : calls) {
                    InlineFunctionInfo IFI;
                    InlineResult res = InlineFunction(*CI, IFI);
                    if (res.isSuccess()) {
                        inlined = true;
                        everChanged = true;
                    }
                }
            }
            return everChanged;
        }
    };
}

char Alloca2RegPass::ID = 0;

static RegisterPass<Alloca2RegPass> X("alloca2reg", "Alloca-to-register pass for minic", false, false);

// Automatically enable the pass.
// http://adriansampson.net/blog/clangpass.html
static void registerAlloca2RegPass(const PassManagerBuilder &,
                                    legacy::PassManagerBase &PM) {
    PM.add(new Alloca2RegPass());
}
static RegisterStandardPasses
        RegisterMyPass(PassManagerBuilder::EP_EarlyAsPossible,
                       registerAlloca2RegPass);