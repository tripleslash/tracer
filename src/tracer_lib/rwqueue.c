
#include <tracer_lib/rwqueue.h>

typedef struct TracerRWQueue {
    int                 mMaxElements;
    int                 mElementSize;
    volatile int        mReadOffset;
    volatile int        mWriteOffset;
    TracerBool          mIsOwnedByOther;
} TracerRWQueue;

TracerHandle tracerCreateRWQueue(void* address, size_t spaceInBytes, size_t elemSize) {
    if (elemSize == 0) {
        tracerCoreSetLastError(eTracerErrorInvalidArgument);
        return NULL;
    }

    if (spaceInBytes < sizeof(TracerRWQueue)) {
        tracerCoreSetLastError(eTracerErrorNotEnoughMemory);
        return NULL;
    }

    int maxElements = (spaceInBytes - sizeof(TracerRWQueue)) / elemSize;
    if (maxElements <= 0) {
        tracerCoreSetLastError(eTracerErrorNotEnoughMemory);
        return NULL;
    }

    TracerBool ownedByOther = eTracerTrue;

    if (!address) {
        address = malloc(spaceInBytes);
        ownedByOther = eTracerFalse;
    }

    if (!address) {
        tracerCoreSetLastError(eTracerErrorNotEnoughMemory);
        return NULL;
    }

    TracerRWQueue* queue = (TracerRWQueue*)address;
    queue->mMaxElements = maxElements;
    queue->mElementSize = elemSize;
    queue->mReadOffset = 0;
    queue->mWriteOffset = 0;
    queue->mIsOwnedByOther = ownedByOther;

    return (TracerHandle)queue;
}

void tracerDestroyRWQueue(TracerHandle handle) {
    TracerRWQueue* queue = (TracerRWQueue*)handle;

    if (!queue) {
        tracerCoreSetLastError(eTracerErrorInvalidArgument);
        return;
    }

    queue->mMaxElements = 0;
    queue->mElementSize = 0;
    queue->mReadOffset = 0;
    queue->mWriteOffset = 0;

    if (!queue->mIsOwnedByOther) {
        free(queue);
    }
}

TracerBool tracerRWQueuePushItem(TracerHandle handle, const void* item) {
    TracerRWQueue* queue = (TracerRWQueue*)handle;

    if (!queue || !item) {
        tracerCoreSetLastError(eTracerErrorInvalidArgument);
        return eTracerFalse;
    }

    int readOffset = queue->mReadOffset;

    if (queue->mWriteOffset == queue->mMaxElements) {
        // The writer has reached the end of the queue. Now check if the reader has
        // finished reading the elements at the beginning of the queue. If yes, then
        // we can start adding elements to the beginning of the queue.

        // If not, we can't add elements until the reader has read something.
        if (readOffset > 1) {
            queue->mWriteOffset = 0;
        } else {
            return eTracerFalse;
        }
    }

    TracerBool isNotAtEnd = (queue->mWriteOffset < queue->mMaxElements);
    TracerBool isReaderBehindWriter = (readOffset <= queue->mWriteOffset);
    TracerBool isWriterBehindReader = (queue->mWriteOffset < readOffset - 1);

    if ((isNotAtEnd && isReaderBehindWriter) || isWriterBehindReader) {
        // We can add elements to the queue in two cases:
        // 1) the writer is not at the end of the queue and he is ahead of the reader
        // 2) the queue is full and the reader has already read elements from the start of the queue

        uint8_t* dataPointer = (uint8_t*)(queue + 1);
        void* writePtr = dataPointer + (queue->mWriteOffset * queue->mElementSize);

        // Copy the item to the queue
        memcpy(writePtr, item, queue->mElementSize);

        // And increment the write pointer
        queue->mWriteOffset++;

        // We are done here
        return eTracerTrue;
    } else {
        // There is not enough space to add the element anywhere!
        return eTracerFalse;
    }
}

TracerBool tracerRWQueuePopItem(TracerHandle handle, void* outItem) {
    TracerRWQueue* queue = (TracerRWQueue*)handle;

    if (!queue || !outItem) {
        tracerCoreSetLastError(eTracerErrorInvalidArgument);
        return eTracerFalse;
    }

    int writeOffset = queue->mWriteOffset;

    if (queue->mReadOffset == queue->mMaxElements) {
        // The reader has reached the end of the queue. We can safely reset
        // his pointer back to the beginning of the queue
        queue->mReadOffset = 0;
    }

    TracerBool isNotAtEnd = (queue->mReadOffset < queue->mMaxElements);
    TracerBool isReaderBehindWriter = (queue->mReadOffset < writeOffset);
    TracerBool isWriterBehindReader = (writeOffset < queue->mReadOffset);

    if ((isNotAtEnd && isWriterBehindReader) || isReaderBehindWriter) {
        // We can read elements from the queue in two cases:
        // 1) the reader is not at the end of the queue and he is ahead of the writer
        // 2) the reader is behind the writer

        const uint8_t* dataPointer = (const uint8_t*)(queue + 1);
        const void* readPtr = dataPointer + (queue->mReadOffset * queue->mElementSize);

        // Copy the item from the queue
        memcpy(outItem, readPtr, queue->mElementSize);

        // And increment the read pointer
        queue->mReadOffset++;

        // We are done here
        return eTracerTrue;
    } else {
        // Can't read anything at the moment
        return eTracerFalse;
    }
}

size_t tracerRWQueuePopAll(TracerHandle handle, void* outItems, size_t maxElements) {
    TracerRWQueue* queue = (TracerRWQueue*)handle;

    if (!queue || !outItems) {
        tracerCoreSetLastError(eTracerErrorInvalidArgument);
        return eTracerFalse;
    }

    size_t numElements = 0;

    while (numElements < maxElements) {
        if (!tracerRWQueuePopItem(handle, outItems)) {
            break;
        }

        numElements++;
        outItems = ((uint8_t*)outItems) + queue->mElementSize;
    }

    return numElements;
}
