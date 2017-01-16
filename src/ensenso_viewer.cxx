#include <thread>
#include <mutex>
#include <memory>
#include <chrono>
#include <iostream>

#include <ros/ros.h>
#include <ros/spinner.h>
#include <cv_bridge/cv_bridge.h>
#include <ensenso/visualizer.h>
#include <ensenso/ensenso_headers.h>

#include <ensenso/pcl_headers.h>

#include <sensor_msgs/Image.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/image_encodings.h>

#include <image_transport/image_transport.h>
#include <image_transport/subscriber_filter.h>

#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

#define OUT(__x__) std::cout << __x__ << std::endl;

/*Globlal namespaces and aliases*/
using namespace pathfinder;

/*aliases*/
using PointT = pcl::PointXYZ;
using PointCloudT = pcl::PointCloud<PointT>;
using pcl_viz = pcl::visualization::PCLVisualizer;

class Receiver
{
private:  
  /*aliases*/
  using imageMsgSub = message_filters::Subscriber<sensor_msgs::Image>;
  using cloudMsgSub = message_filters::Subscriber<sensor_msgs::PointCloud2>;
  using syncPolicy = message_filters::sync_policies::ApproximateTime<sensor_msgs::PointCloud2,sensor_msgs::Image>;

  bool running, updateCloud, updateImage, save;
  size_t counter;
  std::ostringstream oss;

  const std::string cloudName;
  pcl::PCDWriter writer;
  std::vector<int> params;

  ros::NodeHandle nh;
  std::mutex mutex;
  cv::Mat ir;
  std::string windowName;
  const std::string basetopic;
  std::string subNameDepth;
  PointCloudT cloud;  

  unsigned long const hardware_threads;
  ros::AsyncSpinner spinner;
  std::string subNameCloud, subNameIr;
  imageMsgSub subImageIr;
  cloudMsgSub subCloud;

  std::vector<std::thread> threads;

  boost::shared_ptr<pcl_viz> viewer;
  message_filters::Synchronizer<syncPolicy> sync;

  boost::shared_ptr<visualizer> viz;
public:
  //constructor
  Receiver()
  : updateCloud(false), updateImage(false), save(false), counter(0), 
  cloudName("ensenso_cloud"), windowName("Ensenso images"), basetopic("/ensenso"), 
  hardware_threads(std::thread::hardware_concurrency()),  spinner(hardware_threads/2), 
  subNameCloud(basetopic + "/cloud"), subNameIr(basetopic + "/image_combo"), 
  subImageIr(nh, subNameIr, 1), subCloud(nh, subNameCloud, 1),  
  sync(syncPolicy(10), subCloud, subImageIr)
  {
    sync.registerCallback(boost::bind(&Receiver::callback, this, _1, _2));
    ROS_INFO_STREAM("#Hardware Concurrency: " << hardware_threads <<
      "\t. Spinning with " << hardware_threads/4 << " threads");
    params.push_back(cv::IMWRITE_PNG_COMPRESSION);
    params.push_back(3);
  }
  //destructor
  ~Receiver()
  { 

  }

  Receiver(Receiver const&) =delete;
  Receiver& operator=(Receiver const&) = delete;


  void run()
  {
    begin();
    end();
  }
private:
  void begin()
  {      
    if(spinner.canStart())  
    {   
      spinner.start();  
    }
    running = true;
    while(!updateImage || !updateCloud)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }    
    //spawn the threads
    threads.push_back(std::thread(&Receiver::cloudDisp, this));
    threads.push_back(std::thread(&Receiver::imageDisp, this));
    //call join on each thread in turn
    std::for_each(threads.begin(), threads.end(), \
                  std::mem_fn(&std::thread::join)); 
  }

  void end()
  {
    spinner.stop();       
    running = false;
  }

  void callback(const sensor_msgs::PointCloud2ConstPtr& ensensoCloud, const sensor_msgs::ImageConstPtr& ensensoImage)
  {
    cv::Mat ir; 
    PointCloudT cloud;
    getImage(ensensoImage, ir);
    getCloud(ensensoCloud, cloud);

    std::lock_guard<std::mutex> lock(mutex);
    this->ir = ir;
    this->cloud = cloud;
    updateImage = true;
    updateCloud = true;
  }

  void getImage(const sensor_msgs::ImageConstPtr msgImage, cv::Mat &image) const
  {    
    cv_bridge::CvImageConstPtr cv_ptr;
    try
    {
      cv_ptr = cv_bridge::toCvShare(msgImage, sensor_msgs::image_encodings::MONO8);
    }
    catch (cv_bridge::Exception& e)
    {
      ROS_ERROR("cv_bridge exception: %s", e.what());
      return;
    }
    cv_ptr->image.copyTo(image);
  }

  void getCloud(const sensor_msgs::PointCloud2ConstPtr cb_cloud, PointCloudT& pcl_cloud) const
  {
    pcl::PCLPointCloud2 pcl_pc;
    pcl_conversions::toPCL(*cb_cloud, pcl_pc);
    pcl::fromPCLPointCloud2(pcl_pc, pcl_cloud);
  }

  void saveCloudAndImage(const PointCloudT& cloud, const cv::Mat& image)
  {
    auto paths = pathfinder::getCurrentPath();
    boost::filesystem::path pwd = std::get<0>(paths);
    const std::string& train_imgs = std::get<3>(paths);
    const std::string& train_clouds = std::get<4>(paths);
    const std::string& test_imgs = std::get<5>(paths);
    const std::string& test_clouds = std::get<6>(paths);
    ROS_INFO_STREAM("train_imgs: "<< train_imgs << "\ttrain_clouds: " << train_clouds );

    oss.str("");
    oss << counter;
    const std::string baseName = oss.str();
    const std::string cloud_id = "face_positive_" + baseName + "_cloud.pcd";// baseName + "_cloud.pcd";
    const std::string imageName = "face_positive_" + baseName + "_image.png";//baseName + "_image.png";

    ROS_INFO_STREAM("saving cloud: " << cloudName);
    writer.writeBinary(cloud_id, cloud);
    ROS_INFO_STREAM("saving image: " << imageName);
    cv::imwrite(imageName, image, params);

    ROS_INFO_STREAM("saving complete!");
    ++counter;
  }

  void imageDisp()
  {
    cv::Mat ir;  
    PointCloudT cloud; 

    cv::namedWindow(windowName, cv::WINDOW_NORMAL);
    cv::resizeWindow(windowName, 640, 480) ;

    for(; running && ros::ok();)
    {
      if(updateImage)
      {
        std::lock_guard<std::mutex> lock(mutex);
        ir = this->ir;
        cloud = this->cloud;
        updateImage = false;

        cv::imshow(windowName, ir);
      }

      int key = cv::waitKey(1);
      switch(key & 0xFF)
      {
        case 27:
        case 'q':
          running = false;
          break;
        case ' ':
        case 's':
          saveCloudAndImage(cloud, ir);
          save = true;
          break;
      }
    }
    cv::destroyAllWindows();
    cv::waitKey(100);
  }

  void cloudDisp()
  {
    // const PointCloudT cloud (new const PointCloudT);
    const PointCloudT& cloud  = this->cloud;   
    PointCloudT::ConstPtr cloud_ptr (&cloud);

    pcl::visualization::PointCloudColorHandlerCustom<PointT> color_handler (cloud_ptr, 255, 255, 255);
    cv::Mat ir = this->ir;    
    viz = boost::shared_ptr<visualizer> (new visualizer());
    viewer= viz->createViewer();
    viewer->addPointCloud(cloud_ptr, color_handler, cloudName);

    for(; running && ros::ok() ;)
    {
      /*populate the cloud viewer and prepare for publishing*/
      if(updateCloud)
      {   
        std::lock_guard<std::mutex> lock(mutex); 
        updateCloud = false;
        viewer->updatePointCloud(cloud_ptr, cloudName);
      }
      if(save)
      {            
        save = false;
        saveCloudAndImage(cloud, ir);
      }
      viewer->spinOnce(10);
    }
    viewer->close();
  }
};

int main(int argc, char** argv)
{
  ros::init(argc, argv, "ensensor_viewer_node"); 

  ROS_INFO("Started node %s", ros::this_node::getName().c_str());

  Receiver r;
  r.run();

  if(!ros::ok())
  {
    return 0;
  }
  
  ros::shutdown();
}
