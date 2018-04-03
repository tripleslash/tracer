#ifndef TLIB_RWQUEUE_H
#define TLIB_RWQUEUE_H

#include <tracer_lib/core.h>

TracerHandle tracerCreateRWQueue(void* address, size_t spaceInBytes, size_t elemSize);

void tracerDestroyRWQueue(TracerHandle queue);

TracerBool tracerRWQueuePushItem(TracerHandle queue, const void* item);

TracerBool tracerRWQueuePopItem(TracerHandle queue, void* outItem);

size_t tracerRWQueuePopAll(TracerHandle queue, void* outItems, size_t maxElements);

#endif
