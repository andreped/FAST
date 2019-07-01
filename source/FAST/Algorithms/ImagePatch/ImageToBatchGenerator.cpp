#include "ImageToBatchGenerator.hpp"
#include <FAST/Data/Image.hpp>
#include <FAST/Algorithms/NeuralNetwork/NeuralNetwork.hpp>

namespace fast {

ImageToBatchGenerator::ImageToBatchGenerator() {
    createInputPort<Image>(0);
    createOutputPort<Batch>(0);

    m_maxBatchSize = -1;
    m_firstFrameIsInserted = false;
    m_streamIsStarted = false;
    m_stop = false;
    m_hasReachedEnd = false;
}

void ImageToBatchGenerator::generateStream() {
    std::cout << "in thread.." << std::endl;
    std::vector<Image::pointer> imageList;
    imageList.reserve(m_maxBatchSize);
    int i = 0;
    bool lastFrame = false;
    // Update will eventually block, therefore we need to call this in a separate thread
    auto po = mParent->getProcessObject();
    bool firstTime = true;
    while(!lastFrame) {
        {
            std::unique_lock<std::mutex> lock(m_stopMutex);
            if(m_stop) {
                m_streamIsStarted = false;
                m_firstFrameIsInserted = false;
                m_hasReachedEnd = false;
                break;
            }
        }
        std::cout << "WAITING FOR IMAGE.." << std::endl;
        if(!firstTime) // parent is execute the first time, thus drop it here
            po->update(); // Make sure execute is called on previous
        firstTime = false;
        Image::pointer image;
        try {
            image = mParent->getNextFrame<Image>();
        } catch(ThreadStopped &e) {
            break;
        }
        std::cout << "GOT IMAGE.." << std::endl;
        std::cout << "PATCH: " << image->getFrameData("patchid-x") << " " << image->getFrameData("patchid-y") << std::endl;
        lastFrame = image->isLastFrame();
        if(lastFrame)
            std::cout << "LAST FRAME OF STREAM!!!!" << std::endl;
        imageList.push_back(image);
        std::cout << "ADDED IMAGE TO LIST IN BATCH GENERATOR " << imageList.size() << std::endl;
        if(imageList.size() == m_maxBatchSize || lastFrame) {
            std::cout << "CREATING BATCH " << i << std::endl;
            auto batch = Batch::New();
            batch->create(imageList);
            if(lastFrame)
                batch->setLastFrame(getNameOfClass());
            try {
                addOutputData(0, batch);
            } catch(ThreadStopped &e) {
                break;
            }
            imageList.clear();
            i++;
            {
                std::unique_lock<std::mutex> lock(m_firstFrameMutex);
                m_firstFrameIsInserted = true;
            }
            m_firstFrameCondition.notify_all();
        }
    }
    //updateThread.join();
}

void ImageToBatchGenerator::execute() {
    if(m_maxBatchSize == 1)
        throw Exception("Max batch size must be given to the ImageToBatchGenerator");

    if(!m_streamIsStarted) {
        m_streamIsStarted = true;
        mParent = mInputConnections[0];
        mInputConnections.clear(); // Severe the connection
        m_thread = std::make_unique<std::thread>(std::bind(&ImageToBatchGenerator::generateStream, this));
    }

    // Wait here for first frame
    std::unique_lock<std::mutex> lock(m_firstFrameMutex);
    while(!m_firstFrameIsInserted) {
        m_firstFrameCondition.wait(lock);
    }
}

void ImageToBatchGenerator::setMaxBatchSize(int size) {
    if(size <= 0)
        throw Exception("Max batch size must be larger than 0");
    m_maxBatchSize = size;
    mIsModified = true;
}

bool ImageToBatchGenerator::hasReachedEnd() {
    return false;
}

ImageToBatchGenerator::~ImageToBatchGenerator() {
    stop();
}

void ImageToBatchGenerator::stop() {
    {
        std::unique_lock<std::mutex> lock(m_stopMutex);
        m_stop = true;
    }
    m_thread->join();
    reportInfo() << "File streamer thread returned" << reportEnd();
}

}