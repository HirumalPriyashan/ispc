// Copyright 2020-2023 Intel Corporation
// SPDX-License-Identifier: BSD-3-Clause

#include "ispcrt.h"
// std
#include <exception>
#include <iostream>
// ispcrt
#include "detail/Exception.h"
#include "detail/Module.h"
#include "detail/TaskQueue.h"

#ifdef ISPCRT_BUILD_CPU
#include "detail/cpu/CPUDevice.h"
#include "detail/cpu/CPUContext.h"
#endif

#ifdef ISPCRT_BUILD_GPU
#include "detail/gpu/GPUDevice.h"
#include "detail/gpu/GPUContext.h"
#endif

static void defaultErrorFcn(ISPCRTError e, const char *msg) {
    std::cerr << "ISPCRT Error (" << e << "): " << msg << std::endl;
    exit(-1);
}

static ISPCRTErrorFunc g_errorFcn = &defaultErrorFcn;

// Helper functions ///////////////////////////////////////////////////////////

static void handleError(ISPCRTError e, const char *msg) {
    if (g_errorFcn)
        g_errorFcn(e, msg);
}

template <typename OBJECT_T = ispcrt::RefCounted, typename HANDLE_T = ISPCRTGenericHandle>
static OBJECT_T &referenceFromHandle(HANDLE_T handle) {
    return *((OBJECT_T *)handle);
}

#define ISPCRT_CATCH_BEGIN try {
#define ISPCRT_CATCH_END_NO_RETURN()                                                                                   \
    }                                                                                                                  \
    catch (const ispcrt::base::ispcrt_runtime_error &e) {                                                              \
        handleError(e.e, e.what());                                                                                    \
        return;                                                                                                        \
    }                                                                                                                  \
    catch (const std::logic_error &e) {                                                                                \
        handleError(ISPCRT_INVALID_OPERATION, e.what());                                                               \
        return;                                                                                                        \
    }                                                                                                                  \
    catch (const std::exception &e) {                                                                                  \
        handleError(ISPCRT_UNKNOWN_ERROR, e.what());                                                                   \
        return;                                                                                                        \
    }                                                                                                                  \
    catch (...) {                                                                                                      \
        handleError(ISPCRT_UNKNOWN_ERROR, "an unrecognized exception was caught");                                     \
        return;                                                                                                        \
    }

#define ISPCRT_CATCH_END(a)                                                                                            \
    }                                                                                                                  \
    catch (const ispcrt::base::ispcrt_runtime_error &e) {                                                              \
        handleError(e.e, e.what());                                                                                    \
        return a;                                                                                                      \
    }                                                                                                                  \
    catch (const std::logic_error &e) {                                                                                \
        handleError(ISPCRT_INVALID_OPERATION, e.what());                                                               \
        return a;                                                                                                      \
    }                                                                                                                  \
    catch (const std::exception &e) {                                                                                  \
        handleError(ISPCRT_UNKNOWN_ERROR, e.what());                                                                   \
        return a;                                                                                                      \
    }                                                                                                                  \
    catch (...) {                                                                                                      \
        handleError(ISPCRT_UNKNOWN_ERROR, "an unrecognized exception was caught");                                     \
        return a;                                                                                                      \
    }

///////////////////////////////////////////////////////////////////////////////
////////////////////////// API DEFINITIONS ////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

extern "C" {

void ispcrtSetErrorFunc(ISPCRTErrorFunc fcn) { g_errorFcn = fcn; }

///////////////////////////////////////////////////////////////////////////////
// Object lifetime ////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

long long ispcrtUseCount(ISPCRTGenericHandle h) ISPCRT_CATCH_BEGIN {
    auto &obj = referenceFromHandle(h);
    return obj.useCount();
}
ISPCRT_CATCH_END(0)

void ispcrtRelease(ISPCRTGenericHandle h) ISPCRT_CATCH_BEGIN {
    auto &obj = referenceFromHandle(h);
    obj.refDec();
}
ISPCRT_CATCH_END_NO_RETURN()

void ispcrtRetain(ISPCRTGenericHandle h) ISPCRT_CATCH_BEGIN {
    auto &obj = referenceFromHandle(h);
    obj.refInc();
}
ISPCRT_CATCH_END_NO_RETURN()

///////////////////////////////////////////////////////////////////////////////
// Device initialization //////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
static ISPCRTDevice getISPCRTDevice(ISPCRTDeviceType type, ISPCRTContext context, ISPCRTGenericHandle d, uint32_t deviceIdx) ISPCRT_CATCH_BEGIN {
    ispcrt::base::Device *device = nullptr;

    void* nativeContext = nullptr;
    if (context) {
        auto &c = referenceFromHandle<ispcrt::base::Context>(context);
        nativeContext = c.contextNativeHandle();
    }
    switch (type) {
    case ISPCRT_DEVICE_TYPE_AUTO: {
#if defined(ISPCRT_BUILD_GPU) && defined(ISPCRT_BUILD_CPU)
        try {
            device = new ispcrt::GPUDevice;
        } catch (...) {
            if (device)
                delete device;
            device = new ispcrt::CPUDevice;
        }
#elif defined(ISPCRT_BUILD_CPU)
        device = new ispcrt::CPUDevice;
#elif defined(ISPCRT_BUILD_GPU)
        device = new ispcrt::GPUDevice;
#endif
        break;
    }
    case ISPCRT_DEVICE_TYPE_GPU:
#ifdef ISPCRT_BUILD_GPU
        device = new ispcrt::GPUDevice(nativeContext, d, deviceIdx);
#else
        throw std::runtime_error("GPU support not enabled");
#endif
        break;
    case ISPCRT_DEVICE_TYPE_CPU:
#ifdef ISPCRT_BUILD_CPU
        device = new ispcrt::CPUDevice;
#else
        throw std::runtime_error("CPU support not enabled");
#endif
        break;
    default:
        throw std::runtime_error("Unknown device type queried!");
    }

    return (ISPCRTDevice)device;
}
ISPCRT_CATCH_END(0)

ISPCRTDevice ispcrtGetDevice(ISPCRTDeviceType type, uint32_t deviceIdx) {
    return getISPCRTDevice(type, nullptr, nullptr, deviceIdx);
}

ISPCRTDevice ispcrtGetDeviceFromContext(ISPCRTContext context, uint32_t deviceIdx) {
    auto &c = referenceFromHandle<ispcrt::base::Context>(context);
    return getISPCRTDevice(c.getDeviceType(), context, nullptr, deviceIdx);
}

ISPCRTDevice ispcrtGetDeviceFromNativeHandle(ISPCRTContext context, ISPCRTGenericHandle d) {
    auto &c = referenceFromHandle<ispcrt::base::Context>(context);
    return getISPCRTDevice(c.getDeviceType(), context, d, 0);
}

uint32_t ispcrtGetDeviceCount(ISPCRTDeviceType type) ISPCRT_CATCH_BEGIN {
    uint32_t devices = 0;

    switch (type) {
    case ISPCRT_DEVICE_TYPE_AUTO:
        throw std::runtime_error("Device type must be specified");
        break;
    case ISPCRT_DEVICE_TYPE_GPU:
#ifdef ISPCRT_BUILD_GPU
        devices = ispcrt::gpu::deviceCount();
#else
        throw std::runtime_error("GPU support not enabled");
#endif
        break;
    case ISPCRT_DEVICE_TYPE_CPU:
#ifdef ISPCRT_BUILD_CPU
        devices = ispcrt::cpu::deviceCount();
#else
        throw std::runtime_error("CPU support not enabled");
#endif
        break;
    default:
        throw std::runtime_error("Unknown device type queried!");
    }

    return devices;
}
ISPCRT_CATCH_END(0)

void ispcrtGetDeviceInfo(ISPCRTDeviceType type, uint32_t deviceIdx, ISPCRTDeviceInfo *info) ISPCRT_CATCH_BEGIN {
    if (info == nullptr)
        throw std::runtime_error("info cannot be null!");

    switch (type) {
    case ISPCRT_DEVICE_TYPE_AUTO:
        throw std::runtime_error("Device type must be specified");
        break;
    case ISPCRT_DEVICE_TYPE_GPU:
#ifdef ISPCRT_BUILD_GPU
        *info = ispcrt::gpu::deviceInfo(deviceIdx);
#else
        throw std::runtime_error("GPU support not enabled");
#endif
        break;
    case ISPCRT_DEVICE_TYPE_CPU:
#ifdef ISPCRT_BUILD_CPU
        *info = ispcrt::cpu::deviceInfo(deviceIdx);
#else
        throw std::runtime_error("CPU support not enabled");
#endif
        break;
    default:
        throw std::runtime_error("Unknown device type queried!");
    }
}
ISPCRT_CATCH_END_NO_RETURN()

///////////////////////////////////////////////////////////////////////////////
// Context initialization //////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
static ISPCRTContext getISPCRTContext(ISPCRTDeviceType type, ISPCRTGenericHandle c) ISPCRT_CATCH_BEGIN {
    ispcrt::base::Context *context = nullptr;

    switch (type) {
    case ISPCRT_DEVICE_TYPE_AUTO: {
#if defined(ISPCRT_BUILD_GPU) && defined(ISPCRT_BUILD_CPU)
        try {
            context = new ispcrt::GPUContext;
        } catch (...) {
            if (context)
                delete context;
            context = new ispcrt::CPUContext;
        }
#elif defined(ISPCRT_BUILD_CPU)
        context = new ispcrt::CPUContext;
#elif defined(ISPCRT_BUILD_GPU)
        context = new ispcrt::GPUContext;
#endif
        break;
    }
    case ISPCRT_DEVICE_TYPE_GPU:
#ifdef ISPCRT_BUILD_GPU
        context = new ispcrt::GPUContext(c);
#else
        throw std::runtime_error("GPU support not enabled");
#endif
        break;
    case ISPCRT_DEVICE_TYPE_CPU:
#ifdef ISPCRT_BUILD_CPU
        context = new ispcrt::CPUContext;
#else
        throw std::runtime_error("CPU support not enabled");
#endif
        break;
    default:
        throw std::runtime_error("Unknown device type queried!");
    }

    return (ISPCRTContext)context;
}
ISPCRT_CATCH_END(0)

ISPCRTContext ispcrtNewContext(ISPCRTDeviceType type) {
    return getISPCRTContext(type, nullptr);
}

ISPCRTContext ispcrtGetContextFromNativeHandle(ISPCRTDeviceType type, ISPCRTGenericHandle c){
    return getISPCRTContext(type, c);
}
///////////////////////////////////////////////////////////////////////////////
// MemoryViews ////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

ISPCRTMemoryView ispcrtNewMemoryView(ISPCRTDevice d, void *appMemory, size_t numBytes,
                                     ISPCRTNewMemoryViewFlags *flags) ISPCRT_CATCH_BEGIN {
    const auto &device = referenceFromHandle<ispcrt::base::Device>(d);
    if (flags->allocType != ISPCRT_ALLOC_TYPE_SHARED && flags->allocType != ISPCRT_ALLOC_TYPE_DEVICE) {
        throw std::runtime_error("Unsupported memory allocation type requested!");
    }
    return (ISPCRTMemoryView)device.newMemoryView(appMemory, numBytes, flags);
}
ISPCRT_CATCH_END(nullptr)

ISPCRTMemoryView ispcrtNewMemoryViewForContext(ISPCRTContext c, void *appMemory, size_t numBytes,
                                               ISPCRTNewMemoryViewFlags *flags) ISPCRT_CATCH_BEGIN {
    const auto &context = referenceFromHandle<ispcrt::base::Context>(c);
    if (flags->allocType != ISPCRT_ALLOC_TYPE_SHARED) {
        throw std::runtime_error("Only shared memory allocation is allowed for context!");
    }
    return (ISPCRTMemoryView)context.newMemoryView(appMemory, numBytes, flags);
}
ISPCRT_CATCH_END(nullptr)

void *ispcrtHostPtr(ISPCRTMemoryView h) ISPCRT_CATCH_BEGIN {
    auto &mv = referenceFromHandle<ispcrt::base::MemoryView>(h);
    return mv.hostPtr();
}
ISPCRT_CATCH_END(nullptr)

void *ispcrtDevicePtr(ISPCRTMemoryView h) ISPCRT_CATCH_BEGIN {
    auto &mv = referenceFromHandle<ispcrt::base::MemoryView>(h);
    return mv.devicePtr();
}
ISPCRT_CATCH_END(nullptr)

size_t ispcrtSize(ISPCRTMemoryView h) ISPCRT_CATCH_BEGIN {
    auto &mv = referenceFromHandle<ispcrt::base::MemoryView>(h);
    return mv.numBytes();
}
ISPCRT_CATCH_END(0)

ISPCRTAllocationType ispcrtGetMemoryViewAllocType(ISPCRTMemoryView h) ISPCRT_CATCH_BEGIN {
    auto &mv = referenceFromHandle<ispcrt::base::MemoryView>(h);
    return mv.isShared() ? ISPCRTAllocationType::ISPCRT_ALLOC_TYPE_SHARED
                         : ISPCRTAllocationType::ISPCRT_ALLOC_TYPE_DEVICE;
}
ISPCRT_CATCH_END(ISPCRTAllocationType::ISPCRT_ALLOC_TYPE_UNKNOWN)

ISPCRTAllocationType ispcrtGetMemoryAllocType(ISPCRTDevice d, void* memBuffer) ISPCRT_CATCH_BEGIN {
    const auto &device = referenceFromHandle<ispcrt::base::Device>(d);
    return device.getMemAllocType(memBuffer);
}
ISPCRT_CATCH_END(ISPCRTAllocationType::ISPCRT_ALLOC_TYPE_UNKNOWN)

void *ispcrtSharedPtr(ISPCRTMemoryView h) ISPCRT_CATCH_BEGIN {
    auto &mv = referenceFromHandle<ispcrt::base::MemoryView>(h);
    return mv.devicePtr();
}
ISPCRT_CATCH_END(nullptr)

///////////////////////////////////////////////////////////////////////////////
// Modules ////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

ISPCRTModule ispcrtLoadModule(ISPCRTDevice d, const char *moduleFile,
                              ISPCRTModuleOptions moduleOpts) ISPCRT_CATCH_BEGIN {
    const auto &device = referenceFromHandle<ispcrt::base::Device>(d);
    auto module = (ISPCRTModule)device.newModule(moduleFile, moduleOpts);
    return module;

}
ISPCRT_CATCH_END(nullptr)

void ispcrtDynamicLinkModules(ISPCRTDevice d, ISPCRTModule *modules, const uint32_t numModules) ISPCRT_CATCH_BEGIN {
    const auto &device = referenceFromHandle<ispcrt::base::Device>(d);
    device.dynamicLinkModules((ispcrt::base::Module **)modules, numModules);
}
ISPCRT_CATCH_END()

ISPCRTModule ispcrtStaticLinkModules(ISPCRTDevice d, ISPCRTModule *modules, const uint32_t numModules) ISPCRT_CATCH_BEGIN {
    const auto &device = referenceFromHandle<ispcrt::base::Device>(d);
    return (ISPCRTModule)device.staticLinkModules((ispcrt::base::Module **)modules, numModules);
}
ISPCRT_CATCH_END(nullptr)

void *ispcrtFunctionPtr(ISPCRTModule m, const char *name) ISPCRT_CATCH_BEGIN {
    const auto &module = referenceFromHandle<ispcrt::base::Module>(m);
    return module.functionPtr(name);
}
ISPCRT_CATCH_END(nullptr)

ISPCRTKernel ispcrtNewKernel(ISPCRTDevice d, ISPCRTModule m, const char *name) ISPCRT_CATCH_BEGIN {
    const auto &device = referenceFromHandle<ispcrt::base::Device>(d);
    const auto &module = referenceFromHandle<ispcrt::base::Module>(m);
    return (ISPCRTKernel)device.newKernel(module, name);
}
ISPCRT_CATCH_END(nullptr)

///////////////////////////////////////////////////////////////////////////////
// Task queues ////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

ISPCRTTaskQueue ispcrtNewTaskQueue(ISPCRTDevice d) ISPCRT_CATCH_BEGIN {
    const auto &device = referenceFromHandle<ispcrt::base::Device>(d);
    return (ISPCRTTaskQueue)device.newTaskQueue();
}
ISPCRT_CATCH_END(nullptr)

void ispcrtDeviceBarrier(ISPCRTTaskQueue q) ISPCRT_CATCH_BEGIN {
    auto &queue = referenceFromHandle<ispcrt::base::TaskQueue>(q);
    queue.barrier();
}
ISPCRT_CATCH_END_NO_RETURN()

void ispcrtCopyToDevice(ISPCRTTaskQueue q, ISPCRTMemoryView mv) ISPCRT_CATCH_BEGIN {
    auto &queue = referenceFromHandle<ispcrt::base::TaskQueue>(q);
    auto &view = referenceFromHandle<ispcrt::base::MemoryView>(mv);
    queue.copyToDevice(view);
}
ISPCRT_CATCH_END_NO_RETURN()

void ispcrtCopyToHost(ISPCRTTaskQueue q, ISPCRTMemoryView mv) ISPCRT_CATCH_BEGIN {
    auto &queue = referenceFromHandle<ispcrt::base::TaskQueue>(q);
    auto &view = referenceFromHandle<ispcrt::base::MemoryView>(mv);
    queue.copyToHost(view);
}
ISPCRT_CATCH_END_NO_RETURN()

void ispcrtCopyMemoryView(ISPCRTTaskQueue q, ISPCRTMemoryView mvDst, ISPCRTMemoryView mvSrc, const size_t size) ISPCRT_CATCH_BEGIN {
    auto &queue = referenceFromHandle<ispcrt::base::TaskQueue>(q);
    auto &viewDst = referenceFromHandle<ispcrt::base::MemoryView>(mvDst);
    auto &viewSrc = referenceFromHandle<ispcrt::base::MemoryView>(mvSrc);
    if (size > viewDst.numBytes()) {
        throw std::runtime_error("Requested copy size is bigger than destination buffer size!");
    }
    if (size > viewSrc.numBytes()) {
        throw std::runtime_error("Requested copy size is bigger than source buffer size!");
    }
    queue.copyMemoryView(viewDst, viewSrc, size);
}
ISPCRT_CATCH_END_NO_RETURN()

ISPCRTFuture ispcrtLaunch1D(ISPCRTTaskQueue q, ISPCRTKernel k, ISPCRTMemoryView p, size_t dim0) ISPCRT_CATCH_BEGIN {
    return ispcrtLaunch3D(q, k, p, dim0, 1, 1);
}
ISPCRT_CATCH_END(nullptr)

ISPCRTFuture ispcrtLaunch2D(ISPCRTTaskQueue q, ISPCRTKernel k, ISPCRTMemoryView p, size_t dim0,
                            size_t dim1) ISPCRT_CATCH_BEGIN {
    return ispcrtLaunch3D(q, k, p, dim0, dim1, 1);
}
ISPCRT_CATCH_END(nullptr)

ISPCRTFuture ispcrtLaunch3D(ISPCRTTaskQueue q, ISPCRTKernel k, ISPCRTMemoryView p, size_t dim0, size_t dim1,
                            size_t dim2) ISPCRT_CATCH_BEGIN {
    auto &queue = referenceFromHandle<ispcrt::base::TaskQueue>(q);
    auto &kernel = referenceFromHandle<ispcrt::base::Kernel>(k);

    ispcrt::base::MemoryView *params = nullptr;

    if (p)
        params = &referenceFromHandle<ispcrt::base::MemoryView>(p);

    return (ISPCRTFuture)queue.launch(kernel, params, dim0, dim1, dim2);
}
ISPCRT_CATCH_END(nullptr)

void ispcrtSync(ISPCRTTaskQueue q) ISPCRT_CATCH_BEGIN {
    auto &queue = referenceFromHandle<ispcrt::base::TaskQueue>(q);
    queue.sync();
}
ISPCRT_CATCH_END_NO_RETURN()

uint64_t ispcrtFutureGetTimeNs(ISPCRTFuture f) ISPCRT_CATCH_BEGIN {
    if (!f)
        return -1;

    auto &future = referenceFromHandle<ispcrt::base::Future>(f);

    if (!future.valid())
        return -1;

    return future.time();
}
ISPCRT_CATCH_END(-1)

bool ispcrtFutureIsValid(ISPCRTFuture f) ISPCRT_CATCH_BEGIN {
    auto &future = referenceFromHandle<ispcrt::base::Future>(f);
    return future.valid();
}
ISPCRT_CATCH_END(false)

///////////////////////////////////////////////////////////////////////////////
// Native handles//////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

ISPCRTGenericHandle ispcrtPlatformNativeHandle(ISPCRTDevice d) ISPCRT_CATCH_BEGIN {
    const auto &device = referenceFromHandle<ispcrt::base::Device>(d);
    return device.platformNativeHandle();
}
ISPCRT_CATCH_END(nullptr)

ISPCRTGenericHandle ispcrtDeviceNativeHandle(ISPCRTDevice d) ISPCRT_CATCH_BEGIN {
    const auto &device = referenceFromHandle<ispcrt::base::Device>(d);
    return device.deviceNativeHandle();
}
ISPCRT_CATCH_END(nullptr)

ISPCRTGenericHandle ispcrtDeviceContextNativeHandle(ISPCRTDevice d) ISPCRT_CATCH_BEGIN {
    const auto &device = referenceFromHandle<ispcrt::base::Device>(d);
    return device.contextNativeHandle();
}
ISPCRT_CATCH_END(nullptr)

ISPCRTGenericHandle ispcrtContextNativeHandle(ISPCRTContext c) ISPCRT_CATCH_BEGIN {
    const auto &context = referenceFromHandle<ispcrt::base::Context>(c);
    return context.contextNativeHandle();
}
ISPCRT_CATCH_END(nullptr)

ISPCRTGenericHandle ispcrtTaskQueueNativeHandle(ISPCRTTaskQueue q) ISPCRT_CATCH_BEGIN {
    const auto &queue = referenceFromHandle<ispcrt::base::TaskQueue>(q);
    return queue.taskQueueNativeHandle();
}
ISPCRT_CATCH_END(nullptr)
} // extern "C"
