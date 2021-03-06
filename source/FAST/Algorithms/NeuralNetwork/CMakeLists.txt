fast_add_sources(
    NeuralNetwork.cpp
    NeuralNetwork.hpp
    ImageClassificationNetwork.cpp
    ImageClassificationNetwork.hpp
    SegmentationNetwork.cpp
    SegmentationNetwork.hpp
    InferenceEngine.cpp
    InferenceEngine.hpp
    InferenceEngineManager.cpp
    InferenceEngineManager.hpp
    TensorToSegmentation.cpp
    TensorToSegmentation.hpp
    BoundingBoxNetwork.cpp
    BoundingBoxNetwork.hpp
    FlowNetwork.cpp
    FlowNetwork.hpp
)
fast_add_python_interfaces(InferenceEngine.hpp InferenceEngineManager.hpp)
fast_add_python_shared_pointers(InferenceEngine)
fast_add_process_object(NeuralNetwork NeuralNetwork.hpp)
fast_add_process_object(ImageClassificationNetwork ImageClassificationNetwork.hpp)
fast_add_process_object(ClassificationToText ImageClassificationNetwork.hpp)
fast_add_process_object(SegmentationNetwork SegmentationNetwork.hpp)
fast_add_process_object(BoundingBoxNetwork BoundingBoxNetwork.hpp)
fast_add_process_object(FlowNetwork FlowNetwork.hpp)
fast_add_process_object(ImagesToSequence FlowNetwork.hpp)
fast_add_process_object(TensorToSegmentation TensorToSegmentation.hpp)

if(FAST_MODULE_Visualization)
    fast_add_test_sources(
        Tests.cpp
    )
endif()

fast_add_subdirectories(InferenceEngines)
