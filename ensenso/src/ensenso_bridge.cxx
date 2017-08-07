/*
*   MIT License
*
*   Copyright (c) December 2016
*
*   Permission is hereby granted, free of charge, to any person obtaining a copy
*   of this software and associated documentation files (the "Software"), to deal
*   in the Software without restriction, including without limitation the rights
*   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
*   copies of the Software, and to permit persons to whom the Software is
*   furnished to do so, subject to the following conditions:
*
*   The above copyright notice and this permission notice shall be included in all
*   copies or substantial portions of the Software.
*
*   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
*   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
*   SOFTWARE.
*
*
*  Author: Olalekan P. Ogunmolu
*/
#include <memory>
#include <fstream>
#include <algorithm>

#include <sensor_msgs/Image.h>
#include <sensor_msgs/CameraInfo.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/image_encodings.h>

#include <cv_bridge/cv_bridge.h>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <image_transport/image_transport.h>
#include <pcl_conversions/pcl_conversions.h>

/*pcl and cv headers*/
#include <ensenso/pcl_headers.h>
#include <ensenso/ensenso_headers.h>

int getOpenCVType (std::string type)
{
  if (type == "CV_32FC1")
    return CV_32FC1;
  else if (type == "CV_32FC2")
    return CV_32FC2;
  else if (type == "CV_32FC3")
    return CV_32FC3;
  else if (type == "CV_32FC4")
    return CV_32FC4;
  else if (type == "CV_64FC1")
    return CV_64FC1;
  else if (type == "CV_64FC2")
    return CV_64FC2;
  else if (type == "CV_64FC3")
    return CV_64FC3;
  else if (type == "CV_64FC4")
    return CV_64FC4;
  else if (type == "CV_8UC1")
    return CV_8UC1;
  else if (type == "CV_8UC2")
    return CV_8UC2;
  else if (type == "CV_8UC3")
    return CV_8UC3;
  else if (type == "CV_8UC4")
    return CV_8UC4;
  else if (type == "CV_16UC1")
    return CV_16UC1;
  else if (type == "CV_16UC2")
    return CV_16UC2;
  else if (type == "CV_16UC3")
    return CV_16UC3;
  else if (type == "CV_16UC4")
    return CV_16UC4;
  else if (type == "CV_32SC1")
    return CV_32SC1;
  else if (type == "CV_32SC2")
    return CV_32SC2;
  else if (type == "CV_32SC3")
    return CV_32SC3;
  else if (type == "CV_32SC4")
    return CV_32SC4;

  return (-1);
}

/*typedefs*/
using PairOfImages =  std::pair<pcl::PCLImage, pcl::PCLImage>;  //for the Ensenso grabber callback
using PointT = pcl::PointXYZ;
using PointCloudT = pcl::PointCloud<PointT>;

/*pointers*/
pcl::EnsensoGrabber::Ptr ensenso_ptr;

/*Globals*/
sensor_msgs::PointCloud2 pcl2_msg;   //msg to be displayed in rviz
sensor_msgs::CameraInfo left_info, right_info;
ros::Publisher pclPub;
image_transport::Publisher imagePub, leftImagePub, rightImagePub, imageCorrectionPub;
ros::Publisher leftInfoPub, rightInfoPub;
std::string encoding = "mono8";
bool filter = true;
bool print;
NxLibItem camera_;

void initCaptureParams()
{
  const bool auto_exposure = false;
  const bool auto_gain = false;
  const int bining = 1; //Max. fps (3D): 10 (2x Binning: 30) and 64 disparity levels
  const float exposure = 7.1697756185335138;//0.32;
  const bool front_light = true;
  const int gain = 1;
  const bool gain_boost = false;
  const bool hardware_gamma = false;
  const bool hdr = false;
  const int pixel_clock = 24;
  const bool projector = true;
  const int target_brightness = 80;
  const std::string trigger_mode = "Software";    //this is flex mode
  const bool use_disparity_map_area_of_interest = true;  //reduce area of interest to aid faster transfer times

  if(!ensenso_ptr->configureCapture ( auto_exposure,
                     auto_gain,
                     bining,
                     exposure,
                     front_light,
                     gain,
                     gain_boost,
                     hardware_gamma,
                     hdr,
                     pixel_clock,
                     projector ,
                     target_brightness,
                     trigger_mode,
                     use_disparity_map_area_of_interest))
  {
    ROS_INFO("%s", "Could not configure camera parameters");
  }
}

void initPublishers()
{
  ros::NodeHandle nh;
  image_transport::ImageTransport it(nh);
  ROS_INFO("%s", "Initializing Publishers");

  imagePub = it.advertise("/ensenso/image_combo", 10);
  imageCorrectionPub = it.advertise("/ensenso/image_correction_combo", 10);
  leftImagePub = it.advertise("/ensenso/left/image", 10);
  rightImagePub = it.advertise("/ensenso/right/image", 10);

  pclPub = nh.advertise<sensor_msgs::PointCloud2>("/ensenso/cloud", 10);

  leftInfoPub = nh.advertise<sensor_msgs::CameraInfo>("/ensenso/left/cam/info", 2);
  rightInfoPub = nh.advertise<sensor_msgs::CameraInfo>("/ensenso/right/cam/info", 2);
}

void readAndLoadParams()
{
  boost::filesystem::path data_dir, settingsPath;
//  pathfinder::getDataDirectory(data_dir);

  settingsPath = data_dir / "settings_face_filled.json";
  std::string settingsFile = settingsPath.string();

  ROS_INFO_STREAM("Loading settings file: " << settingsFile);
  std::ifstream file("~/catkin_ws/src/sensors/ensenso/ensenso_calib_params.json");

  if (file.is_open() && file.rdbuf())
  {
     // You can use the std::stringstream class to read a textfile:
     std::stringstream buffer;
     buffer << file.rdbuf();
     std::string const& fileContent = buffer.str();

     // ROS_INFO_STREAM("file contents \n"  << fileContent);

     NxLibItem tmp("/tmp");
     tmp.setJson(fileContent);
     if (tmp[itmParameters].exists())
     {
        camera_[itmParameters].setJson(tmp[itmParameters].asJson(), true);
        // writeableNodesOnly = true silently skips
        // read-only nodes instead of failing when
        // encountering a read-only node
        ROS_INFO("Successfully read camera settings file");
     }
     else
     {
        camera_[itmParameters].setJson(tmp.asJson(), true); // with writebleNodesOnly = true, see comment above
     }
     // ROS_INFO_STREAM("camera_[itmParameters] \n" <<
     //                  camera_[itmParameters].setJson(tmp.asJson(), true));
  }
  else
  {
     ROS_INFO("Could not parse json params file");
  }

}

bool initEnsensoParams()
{
  ROS_INFO("%s", "Initializing ensenso camera parameters");
  ensenso_ptr.reset (new pcl::EnsensoGrabber);
  ensenso_ptr->openTcpPort();
  ensenso_ptr->openDevice();
  ensenso_ptr->enumDevices();
  camera_ = ensenso_ptr->camera_;
  ensenso_ptr->configureCapture();
  ROS_INFO("Loading json params");
  //readAndLoadParams();
  return true;
}

void imagetoMsg(const boost::shared_ptr<PairOfImages>& images, sensor_msgs::ImagePtr& msg, \
                sensor_msgs::ImagePtr& left_msg, sensor_msgs::ImagePtr& right_msg,sensor_msgs::ImagePtr& cmsg)
{
  /*Process Image and prepare for publishing*/
  unsigned char *l_image_array = reinterpret_cast<unsigned char *> (&images->first.data[0]);
  unsigned char *r_image_array = reinterpret_cast<unsigned char *> (&images->second.data[0]);

  // ROS_INFO_STREAM("Encoding1: " << images->first.encoding << " | Encoding2: " << images->second.encoding);

  int type1 = getOpenCVType (images->first.encoding);
  int type2 = getOpenCVType(images->second.encoding);

  cv::Mat l_image (images->first.height, images->first.width, type1, l_image_array);
  cv::Mat r_image (images->first.height, images->first.width, type2, r_image_array);

  if(images->first.encoding == "CV_8UC3") {
    type1 = CV_8UC3;
    encoding = "bgr8";
  }

  cv::Mat im (images->first.height, images->first.width * 2, type1);
  cv::Mat imc (images->first.height, images->first.width * 2, type1);

  cv::Mat left_image(images->first.height, images->first.width, type1);
  cv::Mat right_image(images->second.height, images->second.width, type2);

  cv::hconcat(l_image, r_image, im);
/*  im.adjustROI (0, 0, 0, -0.5*images->first.width);
  l_image.copyTo (im);
  im.adjustROI (0, 0, -0.5*images->first.width, 0.5*images->first.width);
  l_image.copyTo (im);
  im.adjustROI (0, 0, 0.5*images->first.width, 0);

  imc.adjustROI (0, 0, 0, -0.5*images->first.width);
  r_image.copyTo (imc);
  imc.adjustROI (0, 0, -0.5*images->first.width, 0.5*images->first.width);
  r_image.copyTo (imc);
  imc.adjustROI (0, 0, 0.5*images->first.width, 0); */

  /*prepare image and pcl to be published for rospy*/
  std_msgs::Header header;
  header.frame_id = "ensenso_image";
  header.stamp = ros::Time::now();
  msg = cv_bridge::CvImage(header, encoding, im).toImageMsg();

  std_msgs::Header c_header;
  c_header.frame_id = "ensenso_image";
  c_header.stamp = ros::Time::now();
  cmsg = cv_bridge::CvImage(c_header, encoding, imc).toImageMsg();

  std_msgs::Header left_header;
  left_header.frame_id = "left_ensenso_image";
  left_header.stamp = ros::Time::now();
  left_msg = cv_bridge::CvImage(left_header, encoding, left_image).toImageMsg();

  std_msgs::Header right_header;
  right_header.frame_id = "right_ensenso_image";
  right_header.stamp = ros::Time::now();
  right_msg = cv_bridge::CvImage(right_header, encoding, right_image).toImageMsg();
}

void ensensoExceptionHandling (const NxLibException &ex,
                 std::string func_nam)
{
  ROS_ERROR ("%s: NxLib error %s (%d) occurred while accessing item %s.\n", func_nam.c_str (), ex.getErrorText ().c_str (), ex.getErrorCode (),
         ex.getItemPath ().c_str ());
  if (ex.getErrorCode () == NxLibExecutionFailed)
  {
    NxLibCommand cmd ("");
    ROS_WARN ("\n%s\n", cmd.result ().asJson (true, 4, false).c_str ());
  }
}

bool getCameraInfo(std::string cam, sensor_msgs::CameraInfo &cam_info)
{
  pcl::EnsensoGrabber grabber;
  camera_ = grabber.camera_;
  try
  {
    cam_info.width = camera_[itmSensor][itmSize][0].asInt();
    cam_info.height = camera_[itmSensor][itmSize][1].asInt();
    cam_info.distortion_model = "plumb_bob";
    // Distortion factors
    cam_info.D.resize(5);
    for(std::size_t i = 0; i < cam_info.D.size(); ++i)
      cam_info.D[i] = camera_[itmCalibration][itmMonocular][cam][itmDistortion][i].asDouble();
    // K and R matrices
    for(std::size_t i = 0; i < 3; ++i)
    {
      for(std::size_t j = 0; j < 3; ++j)
      {
        cam_info.K[3*i+j] = camera_[itmCalibration][itmMonocular][cam][itmCamera][j][i].asDouble();
        cam_info.R[3*i+j] = camera_[itmCalibration][itmDynamic][itmStereo][cam][itmRotation][j][i].asDouble();
      }
    }
    cam_info.P[0] = camera_[itmCalibration][itmDynamic][itmStereo][cam][itmCamera][0][0].asDouble();
    cam_info.P[1] = camera_[itmCalibration][itmDynamic][itmStereo][cam][itmCamera][1][0].asDouble();
    cam_info.P[2] = camera_[itmCalibration][itmDynamic][itmStereo][cam][itmCamera][2][0].asDouble();
    cam_info.P[3] = 0.0;
    cam_info.P[4] = camera_[itmCalibration][itmDynamic][itmStereo][cam][itmCamera][0][1].asDouble();
    cam_info.P[5] = camera_[itmCalibration][itmDynamic][itmStereo][cam][itmCamera][1][1].asDouble();
    cam_info.P[6] = camera_[itmCalibration][itmDynamic][itmStereo][cam][itmCamera][2][1].asDouble();
    cam_info.P[7] = 0.0;
    cam_info.P[10] = 1.0;
    if (cam == "Right")
    {
      double B = camera_[itmCalibration][itmStereo][itmBaseline].asDouble() / 1000.0;
      double fx = cam_info.P[0];
      cam_info.P[3] = (-fx * B);
    }
    return true;
  }
  catch (NxLibException &ex)
  {
    ensensoExceptionHandling (ex, "getCameraInfo");
    return false;
  }
}

void callback (const PointCloudT::Ptr& cloud, \
          const boost::shared_ptr<PairOfImages>& images)
{
  //prepare cloud for rospy publishing
  pcl::toROSMsg(*cloud, pcl2_msg);
  pcl2_msg.header.stamp = ros::Time::now();
  pcl2_msg.header.frame_id = "ensenso_cloud";

  sensor_msgs::ImagePtr msg, left_msg, right_msg, cmsg;
  imagetoMsg(images, msg, left_msg, right_msg, cmsg);

  if(print)
    ROS_INFO("fps: %f", ensenso_ptr->getFramesPerSecond());

  /*Publish the image and cloud*/
  pclPub.publish(pcl2_msg);
  imagePub.publish(msg);
  imageCorrectionPub.publish(cmsg);
  leftImagePub.publish(left_msg);
  rightImagePub.publish(right_msg);
}

int main (int argc, char** argv)
{
  ros::init(argc, argv, "ensensor_bridge_node");
  ros::start();
  ROS_INFO("Started node %s", ros::this_node::getName().c_str());

  if(argv[1] == "-p")
    print = true;

  initPublishers();

  initEnsensoParams();

  boost::function<void(const PointCloudT::Ptr&, const boost::shared_ptr<PairOfImages>&)> f \
                                = boost::bind(&callback, _1, _2);

  ensenso_ptr->start ();
  ensenso_ptr->registerCallback (f);

  ros::spin();

  ensenso_ptr->stop ();
  ensenso_ptr->closeDevice ();
  ensenso_ptr->closeTcpPort ();

  return EXIT_SUCCESS;
}
