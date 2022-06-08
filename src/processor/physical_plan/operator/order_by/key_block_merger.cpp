#include "src/processor/include/physical_plan/operator/order_by/key_block_merger.h"

#include "src/function/comparison/operations/include/comparison_operations.h"

using namespace graphflow::processor;
using namespace graphflow::function::operation;

namespace graphflow {
namespace processor {

MergedKeyBlocks::MergedKeyBlocks(
    uint32_t numBytesPerTuple, uint64_t numTuples, MemoryManager* memoryManager)
    : numBytesPerTuple{numBytesPerTuple}, numTuplesPerBlock{(uint32_t)(
                                              LARGE_PAGE_SIZE / numBytesPerTuple)},
      numTuples{numTuples}, endTupleOffset{numTuplesPerBlock * numBytesPerTuple} {
    auto numKeyBlocks = numTuples / numTuplesPerBlock + (numTuples % numTuplesPerBlock ? 1 : 0);
    for (auto i = 0u; i < numKeyBlocks; i++) {
        keyBlocks.emplace_back(make_shared<DataBlock>(memoryManager));
    }
}

// This constructor is used to convert a keyBlock to a MergedKeyBlocks.
MergedKeyBlocks::MergedKeyBlocks(uint32_t numBytesPerTuple, shared_ptr<DataBlock> keyBlock)
    : numBytesPerTuple{numBytesPerTuple}, numTuplesPerBlock{(uint32_t)(
                                              LARGE_PAGE_SIZE / numBytesPerTuple)},
      numTuples{keyBlock->numTuples}, endTupleOffset{numTuplesPerBlock * numBytesPerTuple} {
    keyBlocks.emplace_back(keyBlock);
}

uint8_t* MergedKeyBlocks::getBlockEndTuplePtr(
    uint32_t blockIdx, uint64_t endTupleIdx, uint32_t endTupleBlockIdx) const {
    assert(blockIdx < keyBlocks.size());
    if (endTupleIdx == 0) {
        return getKeyBlockBuffer(0);
    }
    return blockIdx == endTupleBlockIdx ? getTuple(endTupleIdx - 1) + numBytesPerTuple :
                                          getKeyBlockBuffer(blockIdx) + endTupleOffset;
}

void BlockPtrInfo::updateTuplePtrIfNecessary() {
    if (curTuplePtr == curBlockEndTuplePtr) {
        curBlockIdx++;
        if (curBlockIdx <= endBlockIdx) {
            curTuplePtr = keyBlocks->getKeyBlockBuffer(curBlockIdx);
            curBlockEndTuplePtr =
                keyBlocks->getBlockEndTuplePtr(curBlockIdx, endTupleIdx, endBlockIdx);
        }
    }
}

uint64_t KeyBlockMergeTask::findRightKeyBlockIdx(uint8_t* leftEndTuplePtr) {
    // Find a tuple in the right memory block such that:
    // 1. The value of the current tuple is smaller than the value in leftEndTuple.
    // 2. Either the value of next tuple is larger than the value in leftEndTuple or
    // the current tuple is the last tuple in the right memory block.
    int64_t startIdx = rightKeyBlockNextIdx;
    int64_t endIdx = rightKeyBlock->getNumTuples() - 1;

    while (startIdx <= endIdx) {
        uint64_t curTupleIdx = (startIdx + endIdx) / 2;
        uint8_t* curTuplePtr = rightKeyBlock->getTuple(curTupleIdx);

        if (keyBlockMerger.compareTuplePtr(leftEndTuplePtr, curTuplePtr)) {
            if (curTupleIdx == rightKeyBlock->getNumTuples() - 1 ||
                !keyBlockMerger.compareTuplePtr(
                    leftEndTuplePtr, rightKeyBlock->getTuple(curTupleIdx + 1))) {
                // If the current tuple is the last tuple or the value of next tuple is larger than
                // the value of leftEndTuple, return the curTupleIdx.
                return curTupleIdx;
            } else {
                startIdx = curTupleIdx + 1;
            }
        } else {
            endIdx = curTupleIdx - 1;
        }
    }
    // If such tuple doesn't exist, return -1.
    return -1;
}

unique_ptr<KeyBlockMergeMorsel> KeyBlockMergeTask::getMorsel() {
    // We grab a batch of tuples from the left memory block, then do a binary search on the
    // right memory block to find the range of tuples to merge.
    activeMorsels++;
    if (rightKeyBlockNextIdx >= rightKeyBlock->getNumTuples()) {
        // If there is no more tuples left in the right key block,
        // just append all tuples in the left key block to the result key block.
        auto keyBlockMergeMorsel =
            make_unique<KeyBlockMergeMorsel>(leftKeyBlockNextIdx, leftKeyBlock->getNumTuples(),
                rightKeyBlock->getNumTuples(), rightKeyBlock->getNumTuples());
        leftKeyBlockNextIdx = leftKeyBlock->getNumTuples();
        return keyBlockMergeMorsel;
    }

    auto leftKeyBlockStartIdx = leftKeyBlockNextIdx;
    leftKeyBlockNextIdx += batch_size;

    if (leftKeyBlockNextIdx >= leftKeyBlock->getNumTuples()) {
        // This is the last batch of tuples in the left key block to merge, so just merge it with
        // remaining tuples of the right key block.
        auto keyBlockMergeMorsel = make_unique<KeyBlockMergeMorsel>(leftKeyBlockStartIdx,
            min(leftKeyBlockNextIdx, leftKeyBlock->getNumTuples()), rightKeyBlockNextIdx,
            rightKeyBlock->getNumTuples());
        rightKeyBlockNextIdx = rightKeyBlock->getNumTuples();
        return keyBlockMergeMorsel;
    } else {
        // Conduct a binary search to find the ending index in the right memory block.
        auto leftEndIdxPtr = leftKeyBlock->getTuple(leftKeyBlockNextIdx - 1);
        auto rightEndIdx = findRightKeyBlockIdx(leftEndIdxPtr);

        auto keyBlockMergeMorsel = make_unique<KeyBlockMergeMorsel>(leftKeyBlockStartIdx,
            min(leftKeyBlockNextIdx, leftKeyBlock->getNumTuples()), rightKeyBlockNextIdx,
            rightEndIdx == -1 ? rightKeyBlockNextIdx : ++rightEndIdx);

        if (rightEndIdx != -1) {
            rightKeyBlockNextIdx = rightEndIdx;
        }
        return keyBlockMergeMorsel;
    }
}

void KeyBlockMerger::mergeKeyBlocks(KeyBlockMergeMorsel& keyBlockMergeMorsel) const {
    assert(keyBlockMergeMorsel.leftKeyBlockStartIdx < keyBlockMergeMorsel.leftKeyBlockEndIdx ||
           keyBlockMergeMorsel.rightKeyBlockStartIdx < keyBlockMergeMorsel.rightKeyBlockEndIdx);

    auto leftBlockPtrInfo = BlockPtrInfo(keyBlockMergeMorsel.leftKeyBlockStartIdx,
        keyBlockMergeMorsel.leftKeyBlockEndIdx,
        keyBlockMergeMorsel.keyBlockMergeTask->leftKeyBlock);

    auto rightBlockPtrInfo = BlockPtrInfo(keyBlockMergeMorsel.rightKeyBlockStartIdx,
        keyBlockMergeMorsel.rightKeyBlockEndIdx,
        keyBlockMergeMorsel.keyBlockMergeTask->rightKeyBlock);

    auto resultBlockPtrInfo = BlockPtrInfo(
        keyBlockMergeMorsel.leftKeyBlockStartIdx + keyBlockMergeMorsel.rightKeyBlockStartIdx,
        keyBlockMergeMorsel.leftKeyBlockEndIdx + keyBlockMergeMorsel.rightKeyBlockEndIdx,
        keyBlockMergeMorsel.keyBlockMergeTask->resultKeyBlock);

    while (leftBlockPtrInfo.hasMoreTuplesToRead() && rightBlockPtrInfo.hasMoreTuplesToRead()) {
        uint64_t nextNumBytesToMerge = min(min(leftBlockPtrInfo.getNumBytesLeftInCurBlock(),
                                               rightBlockPtrInfo.getNumBytesLeftInCurBlock()),
            resultBlockPtrInfo.getNumBytesLeftInCurBlock());
        for (auto i = 0; i < nextNumBytesToMerge; i += numBytesPerTuple) {
            if (compareTuplePtr(leftBlockPtrInfo.curTuplePtr, rightBlockPtrInfo.curTuplePtr)) {
                memcpy(resultBlockPtrInfo.curTuplePtr, rightBlockPtrInfo.curTuplePtr,
                    numBytesPerTuple);
                rightBlockPtrInfo.curTuplePtr += numBytesPerTuple;
            } else {
                memcpy(
                    resultBlockPtrInfo.curTuplePtr, leftBlockPtrInfo.curTuplePtr, numBytesPerTuple);
                leftBlockPtrInfo.curTuplePtr += numBytesPerTuple;
            }
            resultBlockPtrInfo.curTuplePtr += numBytesPerTuple;
        }
        leftBlockPtrInfo.updateTuplePtrIfNecessary();
        rightBlockPtrInfo.updateTuplePtrIfNecessary();
        resultBlockPtrInfo.updateTuplePtrIfNecessary();
    }

    copyRemainingBlockDataToResult(rightBlockPtrInfo, resultBlockPtrInfo);
    copyRemainingBlockDataToResult(leftBlockPtrInfo, resultBlockPtrInfo);
}

// This function returns true if the value in the leftTuplePtr is larger than the value in the
// rightTuplePtr.
bool KeyBlockMerger::compareTuplePtrWithStringAndUnstructuredCol(
    uint8_t* leftTuplePtr, uint8_t* rightTuplePtr) const {
    // We can't simply use memcmp to compare tuples if there are string or unstructured columns.
    // We should only compare the binary strings starting from the last compared string column
    // till the next string column.
    uint64_t lastComparedBytes = 0;
    for (auto& stringAndUnstructuredKeyInfo : stringAndUnstructuredKeyColInfo) {
        auto result = memcmp(leftTuplePtr + lastComparedBytes, rightTuplePtr + lastComparedBytes,
            stringAndUnstructuredKeyInfo.colOffsetInEncodedKeyBlock - lastComparedBytes +
                stringAndUnstructuredKeyInfo.getEncodingSize());
        // If both sides are nulls, we can just continue to check the next string or
        // unstructured column.
        auto leftStringAndUnstructuredColPtr =
            leftTuplePtr + stringAndUnstructuredKeyInfo.colOffsetInEncodedKeyBlock;
        auto rightStringAndUnstructuredColPtr =
            rightTuplePtr + stringAndUnstructuredKeyInfo.colOffsetInEncodedKeyBlock;
        if (OrderByKeyEncoder::isNullVal(
                leftStringAndUnstructuredColPtr, stringAndUnstructuredKeyInfo.isAscOrder) &&
            OrderByKeyEncoder::isNullVal(
                rightStringAndUnstructuredColPtr, stringAndUnstructuredKeyInfo.isAscOrder)) {
            lastComparedBytes = stringAndUnstructuredKeyInfo.colOffsetInEncodedKeyBlock +
                                stringAndUnstructuredKeyInfo.getEncodingSize();
            continue;
        }

        // If there is a tie, we need to compare the overflow ptr of strings or the actual
        // unstructured values.
        if (result == 0) {
            if (stringAndUnstructuredKeyInfo.isStrCol) {
                // We do an optimization here to minimize the number of times that we fetch
                // strings from factorizedTable. If both left and right strings are short string,
                // they must equal to each other (since there are no other characters to compare for
                // them). If one string is long string and the other string is short string, the
                // long string must be greater than the short string.
                bool isLeftStrLong = OrderByKeyEncoder::isLongStr(
                    leftStringAndUnstructuredColPtr, stringAndUnstructuredKeyInfo.isAscOrder);
                bool isRightStrLong = OrderByKeyEncoder::isLongStr(
                    rightStringAndUnstructuredColPtr, stringAndUnstructuredKeyInfo.isAscOrder);
                if (!isLeftStrLong && !isRightStrLong) {
                    continue;
                } else if (isLeftStrLong && !isRightStrLong) {
                    return stringAndUnstructuredKeyInfo.isAscOrder;
                } else if (!isLeftStrLong && isRightStrLong) {
                    return !stringAndUnstructuredKeyInfo.isAscOrder;
                }
            }

            auto leftTupleInfo = leftTuplePtr + numBytesToCompare;
            auto rightTupleInfo = rightTuplePtr + numBytesToCompare;
            auto leftBlockIdx = OrderByKeyEncoder::getEncodedFTBlockIdx(leftTupleInfo);
            auto leftBlockOffset = OrderByKeyEncoder::getEncodedFTBlockOffset(leftTupleInfo);
            auto rightBlockIdx = OrderByKeyEncoder::getEncodedFTBlockIdx(rightTupleInfo);
            auto rightBlockOffset = OrderByKeyEncoder::getEncodedFTBlockOffset(rightTupleInfo);

            auto& leftFactorizedTable =
                factorizedTables[OrderByKeyEncoder::getEncodedFTIdx(leftTupleInfo)];
            auto& rightFactorizedTable =
                factorizedTables[OrderByKeyEncoder::getEncodedFTIdx(rightTupleInfo)];
            uint8_t result;
            if (stringAndUnstructuredKeyInfo.isStrCol) {
                auto leftStr = leftFactorizedTable->getData<gf_string_t>(
                    leftBlockIdx, leftBlockOffset, stringAndUnstructuredKeyInfo.colOffsetInFT);
                auto rightStr = rightFactorizedTable->getData<gf_string_t>(
                    rightBlockIdx, rightBlockOffset, stringAndUnstructuredKeyInfo.colOffsetInFT);
                result = (leftStr == rightStr);
                if (result) {
                    // If the tie can't be solved, we need to check the next string or unstructured
                    // column.
                    lastComparedBytes = stringAndUnstructuredKeyInfo.colOffsetInEncodedKeyBlock +
                                        stringAndUnstructuredKeyInfo.getEncodingSize();
                    continue;
                }
                result = leftStr > rightStr;
            } else {
                auto leftUnstr = leftFactorizedTable->getData<Value>(
                    leftBlockIdx, leftBlockOffset, stringAndUnstructuredKeyInfo.colOffsetInFT);
                auto rightUnstr = rightFactorizedTable->getData<Value>(
                    rightBlockIdx, rightBlockOffset, stringAndUnstructuredKeyInfo.colOffsetInFT);
                Equals::operation<Value, Value>(
                    leftUnstr, rightUnstr, result, false /* isLeftNull */, false /* isRightNull */);
                if (result) {
                    // If the tie can't be solved, we need to check the next string or unstructured
                    // column.
                    lastComparedBytes = stringAndUnstructuredKeyInfo.colOffsetInEncodedKeyBlock +
                                        stringAndUnstructuredKeyInfo.getEncodingSize();
                    continue;
                }
                GreaterThan::operation<Value, Value>(
                    leftUnstr, rightUnstr, result, false /* isLeftNull */, false /* isRightNull */);
            }
            return stringAndUnstructuredKeyInfo.isAscOrder == result;
        }
        return result > 0;
    }
    // The string or unstructured tie can't be solved, just add the tuple in the leftMemBlock to
    // resultMemBlock.
    return false;
}

void KeyBlockMerger::copyRemainingBlockDataToResult(
    BlockPtrInfo& blockToCopy, BlockPtrInfo& resultBlock) const {
    while (blockToCopy.curBlockIdx <= blockToCopy.endBlockIdx) {
        uint64_t nextNumBytesToMerge =
            min(blockToCopy.getNumBytesLeftInCurBlock(), resultBlock.getNumBytesLeftInCurBlock());
        for (int i = 0; i < nextNumBytesToMerge; i += numBytesPerTuple) {
            memcpy(resultBlock.curTuplePtr, blockToCopy.curTuplePtr, numBytesPerTuple);
            blockToCopy.curTuplePtr += numBytesPerTuple;
            resultBlock.curTuplePtr += numBytesPerTuple;
        }
        blockToCopy.updateTuplePtrIfNecessary();
        resultBlock.updateTuplePtrIfNecessary();
    }
}

unique_ptr<KeyBlockMergeMorsel> KeyBlockMergeTaskDispatcher::getMorsel() {
    if (isDoneMerge()) {
        return nullptr;
    }
    lock_guard<mutex> keyBlockMergeDispatcherLock{mtx};

    if (!activeKeyBlockMergeTasks.empty() && activeKeyBlockMergeTasks.back()->hasMorselLeft()) {
        // If there are morsels left in the lastMergeTask, just give it to the caller.
        auto morsel = activeKeyBlockMergeTasks.back()->getMorsel();
        morsel->keyBlockMergeTask = activeKeyBlockMergeTasks.back();
        return morsel;
    } else if (sortedKeyBlocks->size() > 1) {
        // If there are no morsels left in the lastMergeTask, we just create a new merge task.
        auto leftKeyBlock = sortedKeyBlocks->front();
        sortedKeyBlocks->pop();
        auto rightKeyBlock = sortedKeyBlocks->front();
        sortedKeyBlocks->pop();
        auto resultKeyBlock = make_shared<MergedKeyBlocks>(leftKeyBlock->getNumBytesPerTuple(),
            leftKeyBlock->getNumTuples() + rightKeyBlock->getNumTuples(), memoryManager);
        auto newMergeTask = make_shared<KeyBlockMergeTask>(
            leftKeyBlock, rightKeyBlock, resultKeyBlock, *keyBlockMerger);
        activeKeyBlockMergeTasks.emplace_back(newMergeTask);
        auto morsel = newMergeTask->getMorsel();
        morsel->keyBlockMergeTask = newMergeTask;
        return morsel;
    } else {
        // There is no morsel can be given at this time, just wait for the ongoing merge
        // task to finish.
        return nullptr;
    }
}

void KeyBlockMergeTaskDispatcher::doneMorsel(unique_ptr<KeyBlockMergeMorsel> morsel) {
    lock_guard<mutex> keyBlockMergeDispatcherLock{mtx};
    // If there is no active and morsels left tin the keyBlockMergeTask, just remove it from
    // the active keyBlockMergeTask and add the result key block to the sortedKeyBlocks queue.
    if ((--morsel->keyBlockMergeTask->activeMorsels) == 0 &&
        !morsel->keyBlockMergeTask->hasMorselLeft()) {
        erase(activeKeyBlockMergeTasks, morsel->keyBlockMergeTask);
        sortedKeyBlocks->emplace(morsel->keyBlockMergeTask->resultKeyBlock);
    }
}

void KeyBlockMergeTaskDispatcher::initIfNecessary(MemoryManager* memoryManager,
    shared_ptr<queue<shared_ptr<MergedKeyBlocks>>> sortedKeyBlocks,
    vector<shared_ptr<FactorizedTable>>& factorizedTables,
    vector<StringAndUnstructuredKeyColInfo>& stringAndUnstructuredKeyColInfo,
    uint64_t numBytesPerTuple) {
    lock_guard<mutex> keyBlockMergeDispatcherLock{mtx};
    if (isInitialized) {
        return;
    }
    isInitialized = true;
    this->memoryManager = memoryManager;
    this->sortedKeyBlocks = sortedKeyBlocks;
    this->keyBlockMerger = make_unique<KeyBlockMerger>(
        factorizedTables, stringAndUnstructuredKeyColInfo, numBytesPerTuple);
}

} // namespace processor
} // namespace graphflow
