#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/dnn.hpp>

#include "tracking_by_matching.hpp"
#include "classificator.h"
#include <iostream>
#include <map>
#include <fstream>


using namespace std;
using namespace cv;
using namespace cv::tbm;

static const char* keys =
{ "{video_name       |../../CV-SUMMER-CAMP/data/dogs.mp4| video name}"
"{start_frame      |50| Start frame                      }"
"{frame_step       |1| Frame step                       }"
"{detector_model   |../../CV-SUMMER-CAMP/data/object_detection/common/mobilenet-ssd/caffe/mobilenet-ssd.caffemodel| Path to detector's Caffe model }"
"{detector_weights |../../CV-SUMMER-CAMP/data/object_detection/common/mobilenet-ssd/caffe/mobilenet-ssd.prototxt| Path to detector's Caffe weights }"
"{desired_class_id |12| The desired class that should be tracked }"
};

static void help()
{
	cout << "\nThis example shows the functionality of \"Tracking-by-Matching\" approach:"
		" detector is used to detect objects on frames, \n"
		"matching is used to find correspondences between new detections and tracked objects.\n"
		"Detection is made by DNN detection network every `--frame_step` frame.\n"
		"Point a .prototxt file of the network as the parameter `--detector_model`, and a .caffemodel file"
		" as the parameter `--detector_weights`.\n"
		"(As an example of such detection network is a popular MobileNet_SSD network trained on VOC dataset.)\n"
		"If `--desired_class_id` parameter is set, the detection result is filtered by class id,"
		" returned by the detection network.\n"
		"(That is, if a detection net was trained on VOC dataset, then to track pedestrians point --desired_class_id=15)\n"
		"Example of <video_name> is in opencv_extra/testdata/cv/tracking/\n"
		"Call:\n"
		"./example_tracking_tracking_by_matching --video_name=<video_name> --detector_model=<detector_model_path> --detector_weights=<detector_weights_path> \\\n"
		"                                       [--start_frame=<start_frame>] \\\n"
		"                                       [--frame_step=<frame_step>] \\\n"
		"                                       [--desired_class_id=<desired_class_id>]\n"
		<< endl;

	cout << "\n\nHot keys: \n"
		"\tq - quit the program\n"
		"\tp - pause/resume video\n";
}

cv::Ptr<ITrackerByMatching> createTrackerByMatchingWithFastDescriptor();

class DnnObjectDetector
{
public:
	DnnObjectDetector(const String& net_caffe_model_path, const String& net_caffe_weights_path,
		int desired_class_id = -1,
		float confidence_threshold = 0.2,
		//the following parameters are default for popular MobileNet_SSD caffe model
		const String& net_input_name = "data",
		const String& net_output_name = "detection_out",
		double net_scalefactor = 0.007843,
		const Size& net_size = Size(300, 300),
		const Scalar& net_mean = Scalar(127.5, 127.5, 127.5),
		bool net_swapRB = false)
		:desired_class_id(desired_class_id),
		confidence_threshold(confidence_threshold),
		net_input_name(net_input_name),
		net_output_name(net_output_name),
		net_scalefactor(net_scalefactor),
		net_size(net_size),
		net_mean(net_mean),
		net_swapRB(net_swapRB)
	{
		net = dnn::readNet(net_caffe_model_path, net_caffe_weights_path);
		if (net.empty())
			CV_Error(Error::StsError, "Cannot read Caffe net");
	}
	TrackedObjects detect(const cv::Mat& frame, int frame_idx)
	{
		Mat resized_frame;
		resize(frame, resized_frame, net_size);
		Mat inputBlob = cv::dnn::blobFromImage(resized_frame, net_scalefactor, net_size, net_mean, net_swapRB);

		net.setInput(inputBlob, net_input_name);
		Mat detection = net.forward(net_output_name);
		Mat detection_as_mat(detection.size[2], detection.size[3], CV_32F, detection.ptr<float>());

		TrackedObjects res;
		for (int i = 0; i < detection_as_mat.rows; i++)
		{
			float cur_confidence = detection_as_mat.at<float>(i, 2);
			int cur_class_id = static_cast<int>(detection_as_mat.at<float>(i, 1));
			int x_left = static_cast<int>(detection_as_mat.at<float>(i, 3) * frame.cols);
			int y_bottom = static_cast<int>(detection_as_mat.at<float>(i, 4) * frame.rows);
			int x_right = static_cast<int>(detection_as_mat.at<float>(i, 5) * frame.cols);
			int y_top = static_cast<int>(detection_as_mat.at<float>(i, 6) * frame.rows);

			Rect cur_rect(x_left, y_bottom, (x_right - x_left), (y_top - y_bottom));

			if (cur_confidence < confidence_threshold)
				continue;
			if ((desired_class_id >= 0) && (cur_class_id != desired_class_id))
				continue;

			//clipping by frame size
			cur_rect = cur_rect & Rect(Point(), frame.size());
			if (cur_rect.empty())
				continue;

			TrackedObject cur_obj(cur_rect, cur_confidence, frame_idx, -1);
			res.push_back(cur_obj);
		}
		return res;
	}
private:
	cv::dnn::Net net;
	int desired_class_id;
	float confidence_threshold;
	String net_input_name;
	String net_output_name;
	double net_scalefactor;
	Size net_size;
	Scalar net_mean;
	bool net_swapRB;
};

cv::Ptr<ITrackerByMatching>
createTrackerByMatchingWithFastDescriptor() {
	cv::tbm::TrackerParams params;

	cv::Ptr<ITrackerByMatching> tracker = createTrackerByMatching(params);

	std::shared_ptr<IImageDescriptor> descriptor_fast =
		std::make_shared<ResizedImageDescriptor>(
			cv::Size(16, 32), cv::InterpolationFlags::INTER_LINEAR);
	std::shared_ptr<IDescriptorDistance> distance_fast =
		std::make_shared<MatchTemplateDistance>();

	tracker->setDescriptorFast(descriptor_fast);
	tracker->setDistanceFast(distance_fast);

	return tracker;
}

String classNames[20] = {
"background",
"aeroplane",
"bicycle",
"bird",
"boat",
"bottle",
"bus",
"car",
"cat",
"chair",
"cow",
"diningtable",
"dog",
"horse"
"motorbike",
"person",
"pottedplant",
"sheep",
"sofa",
"train",
"tvmonitor"
};


int main(int argc, char** argv) {
	CommandLineParser parser(argc, argv, keys);
	cv::Ptr<ITrackerByMatching> tracker = createTrackerByMatchingWithFastDescriptor();

	String video_name = parser.get<String>("video_name");
	int start_frame = parser.get<int>("start_frame");
	int frame_step = parser.get<int>("frame_step");
	String detector_model = parser.get<String>("detector_model");
	String detector_weights = parser.get<String>("detector_weights");
	int desired_class_id = parser.get<int>("desired_class_id");

	String modelPath = "../../CV-SUMMER-CAMP/data/classification/googlenet/v3/tf/googlenet-v3.frozen.pb";
	String configPath = "../../CV-SUMMER-CAMP/data/classification/squeezenet/1.1/caffe/squeezenet1.1.prototxt";
	String labelsPath = "../../CV-SUMMER-CAMP/data/squeezenet1.1.labels";
	int width = 299;
	int height = 299;
	Scalar mean = Scalar(127.5, 127.5, 127.5);
	bool swapRB = false;

	DnnClassificator dcl(modelPath, configPath, labelsPath, width, height, mean, swapRB);
	Mat  image;
	Mat prob;
	Point classIdPoint = Point(0,0) ;
	double confidence = 0;
	int classId = -1;
	//map<int, Scalar> ColorsID[] = { "" };

	if (video_name.empty() || detector_model.empty() || detector_weights.empty())
	{
		help();
		return -1;
	}

	//open the capture
	VideoCapture cap;
	cap.open(video_name);
	cap.set(CAP_PROP_POS_FRAMES, start_frame);
	// Define the codec and create VideoWriter object.The output is stored in 'outcpp.avi' file.
	// Define the fps to be equal to 10. Also frame size is passed.
	Mat img;
	cap >> img;
	VideoWriter video("../../CV-SUMMER-CAMP/data/output.mp4", VideoWriter::fourcc('M', 'P', 'E', 'G'), 30, Size(img.rows, img.cols));

	if (!cap.isOpened())
	{
		help();
		cout << "***Could not initialize capturing...***\n";
		cout << "Current parameter's value: \n";
		parser.printMessage();
		return -1;
	}

	// If you use the popular MobileNet_SSD detector, the default parameters may be used.
	// Otherwise, set your own parameters (net_mean, net_scalefactor, etc).
	DnnObjectDetector detector(detector_model, detector_weights, desired_class_id);

	Mat frame;
	//namedWindow("Tracking by Matching", 1);
	int frame_counter = -1;
	int64 time_total = 0;
	bool paused = false;
	
	double maxConf = 0;
	for (;; )
	{
		if (paused)
		{
			char c = (char)waitKey(30);
			if (c == 'p')
				paused = !paused;
			if (c == 'q')
				break;
			continue;
		}

		cap >> frame;
		if (frame.empty()) {
			break;
		}
		frame_counter++;
		if (frame_counter < start_frame)
			continue;
		if (frame_counter % frame_step != 0)
			continue;


		int64 frame_time = getTickCount();

		TrackedObjects detections = detector.detect(frame, frame_counter);

		// timestamp in milliseconds
		uint64_t cur_timestamp = static_cast<uint64_t>(1000.0 / 30 * frame_counter);
		tracker->process(frame, detections, cur_timestamp);

		frame_time = getTickCount() - frame_time;
		time_total += frame_time;

		// Drawing colored "worms" (tracks).
		// frame = tracker->drawActiveTracks(frame);


		// Drawing all detected objects on a frame by BLUE COLOR
		for (const auto &detection : detections) {
			cv::rectangle(frame, detection.rect, cv::Scalar(255, 0, 0), 3);
		}
		// Drawing tracked detections only by RED color and print ID and detection
		// confidence level.
		for (const auto &detection : tracker->trackedDetections()) {
			cout << detection.object_id << std::endl;
			cv::rectangle(frame, detection.rect, cv::Scalar(0, 0, 255), 1);
			frame(detection.rect).copyTo(image);
			//Image classification
			prob = dcl.Classify(image);
			//Show result
			Mat tmp = prob.reshape(1, 1);

			minMaxLoc(tmp, 0, &confidence, 0, &classIdPoint);
			classId = classIdPoint.x - 1;
			string objClass = "class_" + std::to_string(classId) +"_" + dcl.classesNames[classId];
			string conf = "_conf_" + std::to_string(confidence);
			string str = objClass + " " + conf;
			imshow("win2", image);
			//if(classId >=150 && classId <= 277 && confidence >= 0.5)
			imwrite("../../CV-SUMMER-CAMP/data/dogs/" + dcl.classesNames[classId] + ".png", image);
		}

		imshow("win", frame);
		video.write(frame);
		char c = (char)waitKey(2);
		if (c == 'q')
			break;
		if (c == 'p')
			paused = !paused;
	}

	double s = frame_counter / (time_total / getTickFrequency());
	std::printf("FPS: %f\n", s);
	return 0;
}
