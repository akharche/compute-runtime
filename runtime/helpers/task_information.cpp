/*
 * Copyright (C) 2017-2018 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/built_ins/builtins_dispatch_builder.h"
#include "runtime/command_stream/linear_stream.h"
#include "runtime/command_stream/command_stream_receiver.h"
#include "runtime/command_queue/command_queue.h"
#include "runtime/command_queue/enqueue_common.h"
#include "runtime/device/device.h"
#include "runtime/device_queue/device_queue.h"
#include "runtime/gtpin/gtpin_notify.h"
#include "runtime/helpers/aligned_memory.h"
#include "runtime/helpers/string.h"
#include "runtime/helpers/task_information.h"
#include "runtime/mem_obj/mem_obj.h"
#include "runtime/memory_manager/memory_manager.h"
#include "runtime/memory_manager/surface.h"

namespace OCLRT {
KernelOperation::~KernelOperation() {
    memoryManager.storeAllocation(std::unique_ptr<GraphicsAllocation>(dsh->getGraphicsAllocation()), REUSABLE_ALLOCATION);
    if (ioh.get() == dsh.get()) {
        ioh.release();
    }
    if (ioh) {
        memoryManager.storeAllocation(std::unique_ptr<GraphicsAllocation>(ioh->getGraphicsAllocation()), REUSABLE_ALLOCATION);
    }
    memoryManager.storeAllocation(std::unique_ptr<GraphicsAllocation>(ssh->getGraphicsAllocation()), REUSABLE_ALLOCATION);
    alignedFree(commandStream->getCpuBase());
}

CommandMapUnmap::CommandMapUnmap(MapOperationType op, MemObj &memObj, MemObjSizeArray &copySize, MemObjOffsetArray &copyOffset, bool readOnly,
                                 CommandStreamReceiver &csr, CommandQueue &cmdQ)
    : memObj(memObj), copySize(copySize), copyOffset(copyOffset), readOnly(readOnly), csr(csr), cmdQ(cmdQ), op(op) {
    memObj.incRefInternal();
}

CommandMapUnmap::~CommandMapUnmap() {
    memObj.decRefInternal();
}

CompletionStamp &CommandMapUnmap::submit(uint32_t taskLevel, bool terminated) {
    if (terminated) {
        return completionStamp;
    }

    bool blocking = true;
    auto commandStreamReceiverOwnership = csr.obtainUniqueOwnership();
    auto &queueCommandStream = cmdQ.getCS(0);
    size_t offset = queueCommandStream.getUsed();

    DispatchFlags dispatchFlags;
    dispatchFlags.blocking = blocking;
    dispatchFlags.dcFlush = true;
    dispatchFlags.useSLM = true;
    dispatchFlags.guardCommandBufferWithPipeControl = true;
    dispatchFlags.lowPriority = cmdQ.getPriority() == QueuePriority::LOW;
    dispatchFlags.throttle = cmdQ.getThrottle();
    dispatchFlags.preemptionMode = PreemptionHelper::taskPreemptionMode(cmdQ.getDevice(), nullptr);

    DEBUG_BREAK_IF(taskLevel >= Event::eventNotReady);

    gtpinNotifyPreFlushTask(&cmdQ);

    completionStamp = csr.flushTask(queueCommandStream,
                                    offset,
                                    cmdQ.getIndirectHeap(IndirectHeap::DYNAMIC_STATE, 0u),
                                    cmdQ.getIndirectHeap(IndirectHeap::INDIRECT_OBJECT, 0u),
                                    cmdQ.getIndirectHeap(IndirectHeap::SURFACE_STATE, 0u),
                                    taskLevel,
                                    dispatchFlags,
                                    cmdQ.getDevice());

    cmdQ.waitUntilComplete(completionStamp.taskCount, completionStamp.flushStamp, false);

    if (!memObj.isMemObjZeroCopy()) {
        if (op == MAP) {
            memObj.transferDataToHostPtr(copySize, copyOffset);
        } else if (!readOnly) {
            DEBUG_BREAK_IF(op != UNMAP);
            memObj.transferDataFromHostPtr(copySize, copyOffset);
        }
    }

    return completionStamp;
}

CommandComputeKernel::CommandComputeKernel(CommandQueue &commandQueue, std::unique_ptr<KernelOperation> kernelOperation, std::vector<Surface *> &surfaces,
                                           bool flushDC, bool usesSLM, bool ndRangeKernel, std::unique_ptr<PrintfHandler> printfHandler,
                                           PreemptionMode preemptionMode, Kernel *kernel, uint32_t kernelCount)
    : commandQueue(commandQueue), kernelOperation(std::move(kernelOperation)), flushDC(flushDC), slmUsed(usesSLM),
      NDRangeKernel(ndRangeKernel), printfHandler(std::move(printfHandler)), kernel(kernel),
      kernelCount(kernelCount), preemptionMode(preemptionMode) {
    for (auto surface : surfaces) {
        this->surfaces.push_back(surface);
    }
    UNRECOVERABLE_IF(nullptr == this->kernel);
    kernel->incRefInternal();
}

CommandComputeKernel::~CommandComputeKernel() {
    for (auto surface : surfaces) {
        delete surface;
    }
    surfaces.clear();
    if (kernelOperation->ioh.get() == kernelOperation->dsh.get()) {
        kernelOperation->doNotFreeISH = true;
    }
    kernel->decRefInternal();
}

CompletionStamp &CommandComputeKernel::submit(uint32_t taskLevel, bool terminated) {
    if (terminated) {
        return completionStamp;
    }
    auto &commandStreamReceiver = commandQueue.getDevice().getCommandStreamReceiver();
    bool executionModelKernel = kernel->isParentKernel;
    auto devQueue = commandQueue.getContext().getDefaultDeviceQueue();

    auto commandStreamReceiverOwnership = commandStreamReceiver.obtainUniqueOwnership();

    if (executionModelKernel) {
        while (!devQueue->isEMCriticalSectionFree())
            ;

        devQueue->resetDeviceQueue();
        devQueue->acquireEMCriticalSection();
    }

    auto &commandStream = *kernelOperation->commandStream;
    size_t commandsSize = commandStream.getUsed();
    auto &queueCommandStream = commandQueue.getCS(commandStream.getUsed());
    size_t offset = queueCommandStream.getUsed();
    void *pDst = queueCommandStream.getSpace(commandsSize);
    //transfer the memory to commandStream of the queue.
    memcpy_s(pDst, commandsSize, commandStream.getCpuBase(), commandsSize);

    IndirectHeap *dsh = kernelOperation->dsh.get();
    IndirectHeap *ioh = kernelOperation->ioh.get();
    IndirectHeap *ssh = kernelOperation->ssh.get();

    auto requiresCoherency = false;
    for (auto &surface : surfaces) {
        DEBUG_BREAK_IF(!surface);
        surface->makeResident(commandStreamReceiver);
        requiresCoherency |= surface->IsCoherent;
    }

    if (printfHandler) {
        printfHandler.get()->makeResident(commandStreamReceiver);
    }
    if (currentTimestampPacketNodes) {
        currentTimestampPacketNodes->makeResident(commandStreamReceiver);
    }
    if (previousTimestampPacketNodes) {
        previousTimestampPacketNodes->makeResident(commandStreamReceiver);
    }

    if (executionModelKernel) {
        uint32_t taskCount = commandStreamReceiver.peekTaskCount() + 1;
        devQueue->setupExecutionModelDispatch(*ssh, *dsh, kernel, kernelCount, taskCount, timestamp);

        BuiltIns &builtIns = *this->kernel->getDevice().getExecutionEnvironment()->getBuiltIns();
        SchedulerKernel &scheduler = builtIns.getSchedulerKernel(commandQueue.getContext());

        scheduler.setArgs(devQueue->getQueueBuffer(),
                          devQueue->getStackBuffer(),
                          devQueue->getEventPoolBuffer(),
                          devQueue->getSlbBuffer(),
                          dsh->getGraphicsAllocation(),
                          kernel->getKernelReflectionSurface(),
                          devQueue->getQueueStorageBuffer(),
                          ssh->getGraphicsAllocation(),
                          devQueue->getDebugQueue());

        devQueue->dispatchScheduler(
            commandQueue,
            scheduler,
            preemptionMode,
            ssh,
            dsh);

        scheduler.makeResident(commandStreamReceiver);

        // Update SLM usage
        slmUsed |= scheduler.slmTotalSize > 0;

        this->kernel->getProgram()->getBlockKernelManager()->makeInternalAllocationsResident(commandStreamReceiver);
    }

    DispatchFlags dispatchFlags;
    dispatchFlags.blocking = true;
    dispatchFlags.dcFlush = flushDC;
    dispatchFlags.useSLM = slmUsed;
    dispatchFlags.guardCommandBufferWithPipeControl = true;
    dispatchFlags.GSBA32BitRequired = NDRangeKernel;
    dispatchFlags.requiresCoherency = requiresCoherency;
    dispatchFlags.lowPriority = commandQueue.getPriority() == QueuePriority::LOW;
    dispatchFlags.throttle = commandQueue.getThrottle();
    dispatchFlags.preemptionMode = preemptionMode;
    dispatchFlags.mediaSamplerRequired = kernel->isVmeKernel();
    if (commandStreamReceiver.peekTimestampPacketWriteEnabled()) {
        dispatchFlags.outOfDeviceDependencies = &eventsRequest;
    }

    DEBUG_BREAK_IF(taskLevel >= Event::eventNotReady);

    gtpinNotifyPreFlushTask(&commandQueue);

    completionStamp = commandStreamReceiver.flushTask(queueCommandStream,
                                                      offset,
                                                      *dsh,
                                                      *ioh,
                                                      *ssh,
                                                      taskLevel,
                                                      dispatchFlags,
                                                      commandQueue.getDevice());
    commandQueue.waitUntilComplete(completionStamp.taskCount, completionStamp.flushStamp, false);
    if (printfHandler) {
        printfHandler.get()->printEnqueueOutput();
    }

    return completionStamp;
}

void CommandComputeKernel::setTimestampPacketNode(TimestampPacketContainer &current, TimestampPacketContainer &previous) {
    currentTimestampPacketNodes = std::make_unique<TimestampPacketContainer>(commandQueue.getDevice().getMemoryManager());
    currentTimestampPacketNodes->assignAndIncrementNodesRefCounts(current);

    previousTimestampPacketNodes = std::make_unique<TimestampPacketContainer>(commandQueue.getDevice().getMemoryManager());
    previousTimestampPacketNodes->assignAndIncrementNodesRefCounts(previous);
}

CompletionStamp &CommandMarker::submit(uint32_t taskLevel, bool terminated) {
    if (terminated) {
        return completionStamp;
    }

    bool blocking = true;
    TakeOwnershipWrapper<Device> deviceOwnership(cmdQ.getDevice());

    auto &queueCommandStream = cmdQ.getCS(this->commandSize);
    size_t offset = queueCommandStream.getUsed();

    DispatchFlags dispatchFlags;
    dispatchFlags.blocking = blocking;
    dispatchFlags.dcFlush = shouldFlushDC(clCommandType, nullptr);
    dispatchFlags.lowPriority = cmdQ.getPriority() == QueuePriority::LOW;
    dispatchFlags.throttle = cmdQ.getThrottle();
    dispatchFlags.preemptionMode = PreemptionHelper::taskPreemptionMode(cmdQ.getDevice(), nullptr);
    if (timestampPacketsForPipeControlWrite) {
        dispatchFlags.timestampPacketForPipeControlWrite = timestampPacketsForPipeControlWrite->peekNodes().at(0);
    }

    DEBUG_BREAK_IF(taskLevel >= Event::eventNotReady);

    gtpinNotifyPreFlushTask(&cmdQ);

    completionStamp = csr.flushTask(queueCommandStream,
                                    offset,
                                    cmdQ.getIndirectHeap(IndirectHeap::DYNAMIC_STATE, 0u),
                                    cmdQ.getIndirectHeap(IndirectHeap::INDIRECT_OBJECT, 0u),
                                    cmdQ.getIndirectHeap(IndirectHeap::SURFACE_STATE, 0u),
                                    taskLevel,
                                    dispatchFlags,
                                    cmdQ.getDevice());

    cmdQ.waitUntilComplete(completionStamp.taskCount, completionStamp.flushStamp, false);

    return completionStamp;
}

void CommandMarker::setTimestampPacketsForPipeControlWrite(TimestampPacketContainer &inputNodes) {
    timestampPacketsForPipeControlWrite = std::make_unique<TimestampPacketContainer>(cmdQ.getDevice().getMemoryManager());
    timestampPacketsForPipeControlWrite->assignAndIncrementNodesRefCounts(inputNodes);
}
} // namespace OCLRT
