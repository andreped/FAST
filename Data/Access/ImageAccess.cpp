#include "ImageAccess.hpp"
using namespace fast;

// TODO this object needs a lot more work
ImageAccess::ImageAccess(void* data, bool* accessFlag) {
    mData = data;
    mAccessFlag = accessFlag;
}

ImageAccess::~ImageAccess() {
    *mAccessFlag = false;
}

void* ImageAccess::get() {
    return mData;
}