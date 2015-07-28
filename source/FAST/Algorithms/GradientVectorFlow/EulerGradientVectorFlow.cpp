#include "EulerGradientVectorFlow.hpp"
#include "FAST/Data/Image.hpp"
#include "HelperFunctions.hpp"

namespace fast {

EulerGradientVectorFlow::EulerGradientVectorFlow() {
    createInputPort<Image>(0);
    createOutputPort<Image>(0, OUTPUT_DEPENDS_ON_INPUT, 0);
    mIterations = 0;
    mMu = 0.05f;
    mUse16bitFormat = true;
}

void EulerGradientVectorFlow::setIterations(uint iterations) {
    if(iterations == 0)
        throw Exception("Number of iterations can't be zero in EulerGradientVectorFlow.");
    mIterations = iterations;
}

void EulerGradientVectorFlow::setMuConstant(float mu) {
    if(mu > 0.2 || mu < 0)
        throw Exception("The constant mu must be larger than 0 and smaller than 0.2 in EulerGradientVectorFlow.");
    mMu = mu;
}

float EulerGradientVectorFlow::getMuConstant() const {
    return mMu;
}

void EulerGradientVectorFlow::set16bitStorageFormat() {
    mUse16bitFormat = true;
}

void EulerGradientVectorFlow::set32bitStorageFormat() {
    mUse16bitFormat = false;
}

void EulerGradientVectorFlow::execute() {
    Image::pointer input = getStaticInputData<Image>();

    if((input->getDimensions() == 2 && input->getNrOfComponents() != 2) ||
            (input->getDimensions() == 3 && input->getNrOfComponents() != 3)) {
        throw Exception("Input to EulerGradientVectorFlow must be a vector field. Nr of components > 1.");
    }

    uint iterations = mIterations;
    if(iterations == 0)
        iterations = std::max(input->getWidth(), std::max(input->getHeight(), input->getDepth()));

    OpenCLDevice::pointer device = getMainDevice();
    device->createProgramFromSourceWithName("EulerGradientVectorFlow", std::string(FAST_SOURCE_DIR) + "Algorithms/GradientVectorFlow/EulerGradientVectorFlow.cl");
    cl::Program program = device->getProgram("EulerGradientVectorFlow");

    cl::Context context = device->getContext();
    cl::CommandQueue queue = device->getCommandQueue();
    const uint width = input->getWidth();
    const uint height = input->getHeight();
    const uint depth = input->getDepth();

    // Create output
    Image::pointer output = getStaticOutputData<Image>();
    output->createFromImage(input, device);
    SceneGraph::setParentNode(output, input);

    if(input->getDimensions() == 2) {
        // For 2D images
        cl::ImageFormat storageFormat;
        if(mUse16bitFormat) {
            if(device->isImageFormatSupported(CL_RG, CL_SNORM_INT16, CL_MEM_OBJECT_IMAGE2D)) {
                std::cout << "Using 16 bit floats" << std::endl;
                storageFormat = cl::ImageFormat(CL_RG, CL_SNORM_INT16);
            } else if(device->isImageFormatSupported(CL_RGBA, CL_SNORM_INT16, CL_MEM_OBJECT_IMAGE2D)) {
                std::cout << "Using 16 bit floats" << std::endl;
                storageFormat = cl::ImageFormat(CL_RGBA, CL_SNORM_INT16);
            } else if(device->isImageFormatSupported(CL_RG, CL_FLOAT, CL_MEM_OBJECT_IMAGE2D)) {
                std::cout << "16 bit floats not supported. Using 32 bit instead." << std::endl;
                storageFormat = cl::ImageFormat(CL_RG, CL_FLOAT);
            } else {
                std::cout << "16 bit floats not supported. Using 32 bit instead." << std::endl;
                storageFormat = cl::ImageFormat(CL_RGBA, CL_FLOAT);
            }

        } else {
            std::cout << "Using 32 bit floats" << std::endl;
            // Check if two channel texture is supported
            if(device->isImageFormatSupported(CL_RG, CL_FLOAT, CL_MEM_OBJECT_IMAGE2D)) {
                storageFormat = cl::ImageFormat(CL_RG, CL_FLOAT);
            } else {
                storageFormat = cl::ImageFormat(CL_RGBA, CL_FLOAT);
            }
        }

        cl::Kernel iterationKernel(program, "GVF2DIteration");
        OpenCLImageAccess2D::pointer access = input->getOpenCLImageAccess2D(ACCESS_READ, device);
        cl::Image2D* inputVectorField = access->get();

        // Copy input vector field and create double buffer
        cl::Image2D vectorField(context, CL_MEM_READ_WRITE, storageFormat, width, height);
        cl::Image2D vectorField2(context, CL_MEM_READ_WRITE, storageFormat, width, height);

        if(storageFormat.image_channel_data_type == CL_SNORM_INT16) {
            // Must run init kernel to copy values to 16 bit texture
            cl::Kernel initKernel(program, "GVF2DCopy");
            initKernel.setArg(0, *inputVectorField);
            initKernel.setArg(1, vectorField);
            queue.enqueueNDRangeKernel(
                initKernel,
                cl::NullRange,
                cl::NDRange(width, height),
                cl::NullRange
            );
        } else {
            // Can do regular copy when using 32 bit
            queue.enqueueCopyImage(
                    *inputVectorField,
                    vectorField,
                    oul::createOrigoRegion(),
                    oul::createOrigoRegion(),
                    oul::createRegion(width, height, 1)
            );
        }

        iterationKernel.setArg(0, *inputVectorField);
        iterationKernel.setArg(3, mMu);

        for(int i = 0; i < iterations; ++i) {
            if(i % 2 == 0) {
                iterationKernel.setArg(1, vectorField);
                iterationKernel.setArg(2, vectorField2);
            } else {
                iterationKernel.setArg(1, vectorField2);
                iterationKernel.setArg(2, vectorField);
            }
            queue.enqueueNDRangeKernel(
                iterationKernel,
                cl::NullRange,
                cl::NDRange(width, height),
                cl::NullRange
            );
        }


        // Copy result to output
        OpenCLImageAccess2D::pointer outputAccess = output->getOpenCLImageAccess2D(ACCESS_READ_WRITE, device);
        cl::Image2D* outputCLImage = outputAccess->get();
        if(storageFormat.image_channel_data_type == CL_SNORM_INT16) {
            // Have to convert type back to float
            cl::Kernel resultKernel(program, "GVF2DCopy");
            resultKernel.setArg(0, vectorField);
            resultKernel.setArg(1, *outputCLImage);
            queue.enqueueNDRangeKernel(
                resultKernel,
                cl::NullRange,
                cl::NDRange(width, height),
                cl::NullRange
            );
        } else {
            queue.enqueueCopyImage(
                    vectorField,
                    *outputCLImage,
                    oul::createOrigoRegion(),
                    oul::createOrigoRegion(),
                    oul::createRegion(width, height, 1)
            );
        }
    } else {
        // For 3D images
        cl::ImageFormat storageFormat;
        if(mUse16bitFormat) {
            // Is 16 bit supported?
            if(device->isImageFormatSupported(CL_RGBA, CL_SNORM_INT16, CL_MEM_OBJECT_IMAGE3D)) {
                std::cout << "Using 16 bit floats" << std::endl;
                storageFormat = cl::ImageFormat(CL_RGBA, CL_SNORM_INT16);
            } else {
                std::cout << "16 bit floats not supported. Using 32 bit instead." << std::endl;
                storageFormat = cl::ImageFormat(CL_RGBA, CL_FLOAT);
            }
        } else {
            std::cout << "Using 32 bit floats" << std::endl;
            storageFormat = cl::ImageFormat(CL_RGBA, CL_FLOAT);
        }

        cl::Kernel iterationKernel(program, "GVF3DIteration");
        OpenCLImageAccess3D::pointer access = input->getOpenCLImageAccess3D(ACCESS_READ, device);
        cl::Image3D* inputVectorField = access->get();

        // Copy input vector field and create double buffer
        cl::Image3D vectorField(context, CL_MEM_READ_WRITE, storageFormat, width, height, depth);
        cl::Image3D vectorField2(context, CL_MEM_READ_WRITE, storageFormat, width, height, depth);

        if(storageFormat.image_channel_data_type == CL_SNORM_INT16) {
            // Must run init kernel to copy values to 16 bit texture
            cl::Kernel initKernel(program, "GVF3DCopy");
            initKernel.setArg(0, *inputVectorField);
            initKernel.setArg(1, vectorField);
            queue.enqueueNDRangeKernel(
                initKernel,
                cl::NullRange,
                cl::NDRange(width, height, depth),
                cl::NullRange
            );
        } else {
            // Can do regular copy when using 32 bit
            queue.enqueueCopyImage(
                    *inputVectorField,
                    vectorField,
                    oul::createOrigoRegion(),
                    oul::createOrigoRegion(),
                    oul::createRegion(width, height, depth)
            );
        }

        iterationKernel.setArg(0, *inputVectorField);
        iterationKernel.setArg(3, mMu);

        for(int i = 0; i < iterations; ++i) {
            if(i % 2 == 0) {
                iterationKernel.setArg(1, vectorField);
                iterationKernel.setArg(2, vectorField2);
            } else {
                iterationKernel.setArg(1, vectorField2);
                iterationKernel.setArg(2, vectorField);
            }
            queue.enqueueNDRangeKernel(
                iterationKernel,
                cl::NullRange,
                cl::NDRange(width, height, depth),
                cl::NullRange
            );
        }

        // Copy result to output
        OpenCLImageAccess3D::pointer outputAccess = output->getOpenCLImageAccess3D(ACCESS_READ_WRITE, device);
        cl::Image3D* outputCLImage = outputAccess->get();
        if(storageFormat.image_channel_data_type == CL_SNORM_INT16) {
            cl::Kernel resultKernel(program, "GVF3DCopy");
            resultKernel.setArg(0, vectorField);
            resultKernel.setArg(1, *outputCLImage);
            queue.enqueueNDRangeKernel(
                resultKernel,
                cl::NullRange,
                cl::NDRange(width, height, depth),
                cl::NullRange
            );
        } else {
            queue.enqueueCopyImage(
                    vectorField,
                    *outputCLImage,
                    oul::createOrigoRegion(),
                    oul::createOrigoRegion(),
                    oul::createRegion(width, height, depth)
            );
        }
    }
}

} // end namespace fast