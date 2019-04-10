#include "semantic_SLAM.h"

semantic_slam_ros::semantic_slam_ros()
// : pclViewer (new pcl::visualization::PCLVisualizer ("Point cloud segmented"))
{
    std::cout << "semantic SLAM constructor " << std::endl;
}

semantic_slam_ros::~semantic_slam_ros()
{
    std::cout << "semantic SLAM destructor " << std::endl;
}

void semantic_slam_ros::readGroundTruthPoint()
{

    std::ifstream infile(text_file_);
    if(!infile.is_open())
    {
        std::cerr << "cannot open the ground truth file " << std::endl;
    }

    points_vec_.clear();
    geometry_msgs::Point points;
    while(infile >> points.x >>  points.y >>  points.z)
    {
        std::cout << "X: " << points.x  << "Y: " << points.y  << "Z: " << points.z  << std::endl;

        points_vec_.push_back(points);
    }

}

void semantic_slam_ros::init()
{
    imu_data_available_ = false, object_detection_available_ = false, point_cloud_available_ = false;
    rvio_pose_available_ = false, imu_first_yaw_ = false;

    prev_time_ = 0;
    RVIO_pose_.resize(6);
    RVIO_pose_.setZero();

    first_yaw_ = 0;

    //this is temporary will be filled from an xml file
    //object pose
    std::vector<particle_filter::object_info_struct_pf> mapped_object_vec;

    std::vector<Eigen::Vector3f> mapped_object_pose;

    mapped_object_vec_.clear();
    mapped_object_pose.clear();

    new_mapped_object_vec_.clear();

    filtered_pose_ = particle_filter_obj_.init(state_size_, num_particles_, mapped_object_vec);
    final_pose_.resize(6); final_pose_.setZero();

    //clearing all the mapped pointcloud data
    mapped_point_cloud_pcl_.clear();
    mapped_point_cloud_pcl_.resize(num_particles_);
    //Display Window
    //    pclViewer->setBackgroundColor (0, 0, 0);
    //    pclViewer->initCameraParameters ();
    //    pclViewer->setCameraPosition(0,0,0,0,0,1,0,-1,0);
    //    vtkSmartPointer<vtkRenderWindow> renderWindowNormals = pclViewer->getRenderWindow();
    //    renderWindowNormals->SetSize(800,450);
    //    renderWindowNormals->Render();


    //Read ros params
    ros::param::get("~text_file", text_file_);
    if(text_file_.length() == 0)
        text_file_ = "/home/hriday/workspace/ros/rovio_semantic_workspace/src/semantic_SLAM/text_files/ground_truth_points.txt";
    std::cout << "text file " << text_file_ << std::endl;

    return;

}

void semantic_slam_ros::run()
{

    current_time_ = (double) ros::Time::now().sec + ((double) ros::Time::now().nsec / (double) 1E9);

    //std::cout << "current time  " << current_time_ << std::endl;
    float time_diff;

    time_diff = current_time_ - prev_time_;
    //std::cout << "time diff " << time_diff << std::endl;

    //--------------------------Prediction stage using ROVIO--------------------------------------//
    if(rvio_pose_available_)
    {
        Eigen::VectorXf RVIO_pose;
        RVIO_pose.resize(6), RVIO_pose.setZero();

        this->getRVIOPose(RVIO_pose);

        //std::cout << "VO_pose data " << VO_pose << std::endl;
        particle_filter_obj_.predictionVO(time_diff, RVIO_pose, final_pose_);

    }

    //    if(imu_data_available_)
    //    {
    //        float roll, pitch, yaw;
    //        this->getIMUdata(roll, pitch, yaw);

    //        particle_filter_obj_.IMUUpdate(roll, pitch, yaw, final_pose_);
    //        //std::cout << "here " << std::endl;
    //    }

    //-------------------------------------segmentation of the point_cloud------------------------------------------//
    std::vector<particle_filter::object_info_struct_pf> complete_obj_info_vec;
    std::vector<particle_filter::all_object_info_struct_pf> all_obj_complete_info_vec;
    std::vector<particle_filter::object_info_struct_all_points_pf> new_complete_obj_info_vec;
    Eigen::Matrix4f transformation_mat;
    if(object_detection_available_)
    {
        if(point_cloud_available_)
        {
            //complete_obj_info_vec     = this->segmentPointCloudData();
            all_obj_complete_info_vec   = this->segmentallPointCloudData(transformation_mat);
        }

        if(!all_obj_complete_info_vec.empty())
        {
            particle_filter_obj_.AllObjectMapAndUpdate(all_obj_complete_info_vec,
                                                       final_pose_,
                                                       transformation_mat,
                                                       all_mapped_object_vec_);

            //publishNewMappedObjects(new_mapped_object_vec_);
        }

    }

    all_particles_= particle_filter_obj_.getAllParticles();

    publishFinalPose();
    publishCorresVOPose();
    publishParticlePoses();
    publishBestParticleMap(all_particles_);
    //publishMappedObjects(mapped_object_vec_);
    //publishAllMappedObjects(all_mapped_object_vec_);


    publishGroundTruthPoints(points_vec_);

    prev_time_ = current_time_;

}

void semantic_slam_ros::open(ros::NodeHandle n)
{

    //ros subsriber
    rovio_odometry_sub_    = n.subscribe("/rovio/odometry", 1, &semantic_slam_ros::rovioOdometryCallback, this);
    imu_sub_               = n.subscribe("/imu/data", 1, &semantic_slam_ros::imuCallback, this);
    //detected_object_sub_   = n.subscribe("/retinanet/bbs",1, &semantic_slam_ros::detectedObjectCallback, this);
    //detected_object_sub_   = n.subscribe("/darknet_ros/bounding_boxes",1, &semantic_slam_ros::detectedObjectDarknetCallback, this);
    detected_object_sub_   = n.subscribe("/darknet_ros/detected_objects",1, &semantic_slam_ros::detectedObjectCallback, this);
    point_cloud_sub_       = n.subscribe("/depth_registered/points", 1, &semantic_slam_ros::pointCloudCallback, this);
    optitrack_pose_sub_    = n.subscribe("/vrpn_client_node/realsense/pose", 1, &semantic_slam_ros::optitrackPoseCallback, this);
    //this is just for visualizing the path
    optitrack_pose_sub_for_plottin_path_ = n.subscribe("/optitrack_pose",1, &semantic_slam_ros::optitrackPoseForPlottingPathCallback, this);


    //Publishers
    particle_poses_pub_             = n.advertise<geometry_msgs::PoseArray>("particle_poses",1);
    final_pose_pub_                 = n.advertise<geometry_msgs::PoseStamped>("final_pose",1);
    final_path_pub_                 = n.advertise<nav_msgs::Path>("final_path",1);
    corres_vo_pose_pub_             = n.advertise<geometry_msgs::PoseStamped>("corres_vo_pose",1);
    corres_vo_path_                 = n.advertise<nav_msgs::Path>("corres_vo_path",1);
    segmented_point_cloud_pub_      = n.advertise<sensor_msgs::PointCloud2>("segmented_point_cloud",1);
    detected_object_point_pub_      = n.advertise<geometry_msgs::PointStamped>("detected_object_pose_cam", 1);
    mapped_objects_visualizer_pub_  = n.advertise<visualization_msgs::MarkerArray>("mapped_objects",1);
    detected_planes_pub_            = n.advertise<visualization_msgs::MarkerArray>("detected_objects",1);
    detected_planes_point_cloud_pub_= n.advertise<sensor_msgs::PointCloud2>("detected_planes_point_cloud",1);
    mapped_points_pub_              = n.advertise<sensor_msgs::PointCloud2>("mapped_points",1);
    optitrack_pose_pub_             = n.advertise<geometry_msgs::PoseStamped>("optitrack_pose",1);
    optitrack_path_pub_             = n.advertise<nav_msgs::Path>("optitrack_path",1);
    ground_truth_points_pub_        = n.advertise<visualization_msgs::MarkerArray>("ground_truth_points",1);

    init();
    readGroundTruthPoint();
}

void semantic_slam_ros::rovioOdometryCallback(const nav_msgs::Odometry &msg)
{

    //converting quaternions to euler angles
    tf::Quaternion q(msg.pose.pose.orientation.x, msg.pose.pose.orientation.y, msg.pose.pose.orientation.z, msg.pose.pose.orientation.w);
    tf::Matrix3x3 m(q);

    double yaw, pitch, roll;
    m.getEulerYPR(yaw, pitch, roll);

    //    std::cout << "yaw " << yaw << std::endl
    //              << "pitch " << pitch << std::endl
    //              << "roll "  << roll << std::endl;

    //this RVIO publishes poses in the world frame required by semantic slam so no conversion required
    Eigen::VectorXf RVIO_pose;
    RVIO_pose.resize(6), RVIO_pose.setZero();

    RVIO_pose(0) = msg.pose.pose.position.x;
    RVIO_pose(1) = msg.pose.pose.position.y;
    RVIO_pose(2) = msg.pose.pose.position.z;
    RVIO_pose(3) = roll;
    RVIO_pose(4) = pitch;
    RVIO_pose(5) = yaw;

    this->setRVIOPose(RVIO_pose);

}

void semantic_slam_ros::setRVIOPose(Eigen::VectorXf RVIO_pose)
{
    rvio_pose_lock_.lock();
    rvio_pose_available_ = true;
    RVIO_pose_ = RVIO_pose;
    rvio_pose_lock_.unlock();
}

void semantic_slam_ros::getRVIOPose(Eigen::VectorXf &RVIO_pose)
{
    rvio_pose_lock_.lock();
    rvio_pose_available_ = false;
    RVIO_pose = RVIO_pose_;
    rvio_pose_lock_.unlock();
}

void semantic_slam_ros::imuCallback(const sensor_msgs::Imu &msg)
{
    //getting imu angles
    tf::Quaternion q(msg.orientation.x, msg.orientation.y, msg.orientation.z, msg.orientation.w);
    tf::Matrix3x3 m(q);

    double yaw, pitch, roll;
    m.getEulerYPR(yaw, pitch, roll);

    if(!imu_first_yaw_)
    {
        first_yaw_ = yaw;
        imu_first_yaw_ = true;
    }

    yaw = yaw - first_yaw_;

    this->setIMUdata(roll, pitch, yaw);
    //this->calculateRPYAngles();

}

void semantic_slam_ros::setIMUdata(float roll, float pitch, float yaw)
{
    imu_lock_.lock();
    this->imu_roll_ = roll;
    this->imu_pitch_ = pitch;
    this->imu_yaw_ = yaw;
    imu_data_available_ = true;
    imu_lock_.unlock();

}

void semantic_slam_ros::getIMUdata(float &roll, float &pitch, float &yaw)
{
    imu_lock_.lock();
    roll = this->imu_roll_;
    pitch = this->imu_pitch_;
    yaw = this->imu_yaw_;
    imu_data_available_ = false;
    imu_lock_.unlock();

}

//void semantic_slam_ros::detectedObjectCallback(const semantic_SLAM::DetectedObjects &msg)
//{
//    //std::cout << "objects size " << msg.objects.size() << std::endl;
//    std::vector<semantic_SLAM::ObjectInfo>  object_info;
//    object_info.resize(msg.objects.size());

//    for(int i =0; i < msg.objects.size(); ++i)
//    {
//        object_info[i].type   = msg.objects[i].type;
//        object_info[i].tl_x   = msg.objects[i].tl_x;
//        object_info[i].tl_y   = msg.objects[i].tl_y;
//        object_info[i].height = msg.objects[i].height;
//        object_info[i].width  = msg.objects[i].width;
//        object_info[i].prob   = msg.objects[i].prob;
//    }

//    this->setDetectedObjectInfo(object_info);


//}

void semantic_slam_ros::detectedObjectCallback(const semantic_SLAM::DetectedObjects &msg)
{
    //std::cout << "objects size " << msg.objects.size() << std::endl;
    std::vector<semantic_SLAM::ObjectInfo>  object_info;
    object_info.resize(msg.objects.size());

    for(int i =0; i < msg.objects.size(); ++i)
    {
        object_info[i].type   = msg.objects[i].type;
        object_info[i].tl_x   = msg.objects[i].tl_x;
        object_info[i].tl_y   = msg.objects[i].tl_y;
        object_info[i].height = msg.objects[i].height;
        object_info[i].width  = msg.objects[i].width;
        object_info[i].prob   = msg.objects[i].prob;
    }

    this->setDetectedObjectInfo(object_info);

}



//void semantic_slam_ros::detectedObjectDarknetCallback(const darknet_ros_msgs::BoundingBoxes& msg)
//{
//    //std::cout << "objects size " << msg.objects.size() << std::endl;
//    std::vector<semantic_SLAM::ObjectInfo>  object_info;
//    object_info.resize(msg.bounding_boxes.size());

//    for(int i =0; i < msg.bounding_boxes.size(); ++i)
//    {
//        object_info[i].type   = msg.bounding_boxes[i].Class;
//        object_info[i].tl_x   = msg.bounding_boxes[i].xmin;
//        object_info[i].tl_y   = msg.bounding_boxes[i].ymin;
//        object_info[i].height = abs(msg.bounding_boxes[i].ymax - msg.bounding_boxes[i].ymin);
//        object_info[i].width  = abs(msg.bounding_boxes[i].xmax - msg.bounding_boxes[i].xmin);
//        object_info[i].prob   = msg.bounding_boxes[i].probability;
//    }

//    this->setDetectedObjectInfo(object_info);


//}

void semantic_slam_ros::setDetectedObjectInfo(std::vector<semantic_SLAM::ObjectInfo> object_info)
{
    detected_object_lock_.lock();
    object_detection_available_ = true;
    this->object_info_ = object_info;
    detected_object_lock_.unlock();
}

void semantic_slam_ros::getDetectedObjectInfo(std::vector<semantic_SLAM::ObjectInfo> &object_info)
{
    detected_object_lock_.lock();
    object_detection_available_ = false;
    object_info = this->object_info_;
    detected_object_lock_.unlock();
}

void semantic_slam_ros::pointCloudCallback(const sensor_msgs::PointCloud2 &msg)
{
    //    pcl::PointCloud<pcl::PointXYZRGB> point_cloud;
    //    pcl::fromROSMsg(msg, point_cloud);

    this->setPointCloudData(msg);
}

void semantic_slam_ros::setPointCloudData(sensor_msgs::PointCloud2 point_cloud)
{
    point_cloud_lock_.lock();
    point_cloud_available_ = true;
    this->point_cloud_msg_ = point_cloud;
    point_cloud_lock_.unlock();

}

void semantic_slam_ros::getPointCloudData(sensor_msgs::PointCloud2 &point_cloud)
{
    point_cloud_lock_.lock();
    point_cloud_available_ = false;
    point_cloud = this->point_cloud_msg_;
    point_cloud_lock_.unlock();

}

void semantic_slam_ros::optitrackPoseCallback(const nav_msgs::Odometry &msg)
{
    geometry_msgs::PoseStamped optitrack_pose;
    optitrack_pose.header.stamp = msg.header.stamp;
    optitrack_pose.header.frame_id = "map";

    optitrack_pose.pose.position.x = msg.pose.pose.position.x ;
    optitrack_pose.pose.position.y = msg.pose.pose.position.y ;
    optitrack_pose.pose.position.z = msg.pose.pose.position.z ;

    optitrack_pose_pub_.publish(optitrack_pose);
}

void semantic_slam_ros::optitrackPoseForPlottingPathCallback(const geometry_msgs::PoseStamped &msg)
{
    nav_msgs::Path optitrack_path;
    geometry_msgs::PoseStamped optitrack_pose;
    ros::Time current_time = ros::Time::now();

    optitrack_pose.header.stamp = current_time;
    optitrack_pose.header.frame_id = "map";
    optitrack_pose.pose.position = msg.pose.position;
    optitrack_pose.pose.orientation = msg.pose.orientation;


    optitrack_pose_vec_.push_back(optitrack_pose);

    optitrack_path.header.stamp = current_time;
    optitrack_path.header.frame_id = "map";
    optitrack_path.poses = optitrack_pose_vec_;
    optitrack_path_pub_.publish(optitrack_path);


}

std::vector<particle_filter::all_object_info_struct_pf> semantic_slam_ros::segmentallPointCloudData(Eigen::Matrix4f& transformation_mat)
{
    sensor_msgs::PointCloud2 segmented_point_cloud;
    std::vector<plane_segmentation::segmented_objects> segmented_objects_from_point_cloud;
    std::vector<particle_filter::all_object_info_struct_pf> complete_obj_info_vec;

    complete_obj_info_vec.clear();

    std::vector<semantic_SLAM::ObjectInfo> object_info;
    this->getDetectedObjectInfo(object_info);

    sensor_msgs::PointCloud2 point_cloud;
    this->getPointCloudData(point_cloud);

    transformation_mat.setOnes();
    semantic_tools_obj_.transformNormalsToWorld(final_pose_,
                                                transformation_mat,
                                                real_sense_pitch_angle);

    //This segments the PC according to the received bounding box data in the 2D image
    segmented_objects_from_point_cloud.clear();
    for (int i = 0; i < object_info.size(); ++i)
    {
        if(object_info[i].type == "chair" || object_info[i].type == "tvmonitor" || object_info[i].type == "book"
                || object_info[i].type == "keyboard" || object_info[i].type == "laptop")
        {
            plane_segmentation::segmented_objects single_segmented_object_from_point_cloud;
            single_segmented_object_from_point_cloud = plane_segmentation_obj_.segmentPointCloudData(object_info[i], point_cloud, segmented_point_cloud);

            if(single_segmented_object_from_point_cloud.type != "spurious")
                segmented_objects_from_point_cloud.push_back(single_segmented_object_from_point_cloud);
        }
    }

    for(int i = 0; i < segmented_objects_from_point_cloud.size(); ++i)
    {
        std::vector<particle_filter::all_object_info_struct_pf> complete_chair_info_vec;
        std::vector<particle_filter::all_object_info_struct_pf> complete_monitor_info_vec;
        std::vector<particle_filter::all_object_info_struct_pf> complete_book_keyboard_info_vec;

        complete_chair_info_vec.clear(), complete_monitor_info_vec.clear(), complete_book_keyboard_info_vec.clear();

        if(!segmented_objects_from_point_cloud[i].segmented_point_cloud->empty())
        {

            //This calculates the normals of the segmented pointcloud
            pcl::PointCloud<pcl::Normal>::Ptr segmented_point_cloud_normal(new pcl::PointCloud<pcl::Normal>);
            segmented_point_cloud_normal = plane_segmentation_obj_.computeNormalsFromPointCloud(segmented_objects_from_point_cloud[i].segmented_point_cloud);

            if(segmented_point_cloud_normal->empty())
                continue;

            //this computes all the planar surfaces from the detected segmented data
            complete_obj_info_vec = this->segmentPlanarSurfaces(segmented_objects_from_point_cloud[i].segmented_point_cloud,
                                                                segmented_point_cloud_normal,
                                                                transformation_mat,
                                                                segmented_objects_from_point_cloud[i].type,
                                                                segmented_objects_from_point_cloud[i].prob);


        }

    }

    //pclViewer->removeAllPointClouds();
    ////pcl::visualization::PointCloudColorHandlerRGBField<pcl::PointXYZ> rgb(kmeans_segmented_points);
    //pclViewer->addPointCloud<pcl::PointXYZRGB>(segmented_point_cloud_pcl,"segmented cloud");
    //pclViewer->addPointCloudNormals<pcl::PointXYZRGB, pcl::Normal>(segmented_point_cloud_pcl, segmented_point_cloud_normal, 100, 0.02f, "segmented cloud normals");
    //pclViewer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 1, "segmented cloud");
    //publishSegmentedPointCloud(segmented_point_cloud);

    this->publishDetectedPlanes(complete_obj_info_vec);
    //this->publishDetectedPlanesPointCloud(complete_obj_info_vec);

    return complete_obj_info_vec;

}

std::vector<particle_filter::all_object_info_struct_pf> semantic_slam_ros::segmentPlanarSurfaces(pcl::PointCloud<pcl::PointXYZRGB>::Ptr segmented_point_cloud,
                                                                                                 pcl::PointCloud<pcl::Normal>::Ptr segmented_point_cloud_normal,
                                                                                                 Eigen::Matrix4f transformation_mat,
                                                                                                 std::string object_type,
                                                                                                 float prob)
{

    std::vector<particle_filter::all_object_info_struct_pf> complete_obj_info_vec;
    complete_obj_info_vec.clear();

    std::vector<cv::Mat> final_pose_from_plane_vec = plane_segmentation_obj_.clusterAndSegmentAllPlanes(segmented_point_cloud,
                                                                                                        segmented_point_cloud_normal,
                                                                                                        transformation_mat);

    //    std::vector<cv::Mat> final_pose_from_plane_vec;

    //    final_pose_from_plane_vec = plane_segmentation_obj_.multiPlaneSegmentation(segmented_point_cloud,
    //                                                                               segmented_point_cloud_normal,
    //                                                                               transformation_mat);

    //    std::vector<cv::Mat> final_pose_from_ransac = plane_segmentation_obj_.computeAllPlanes(segmented_point_cloud,
    //                                                                                           transformation_mat);

    if(!final_pose_from_plane_vec.empty())
    {
        for(int j = 0; j < final_pose_from_plane_vec.size(); ++j)
        {
            Eigen::Vector4f final_detected_point_cam_frame, final_detected_point_robot_frame;
            final_detected_point_cam_frame.setOnes(), final_detected_point_robot_frame.setOnes();

            final_detected_point_cam_frame(0) = final_pose_from_plane_vec[j].at<float>(0,0);
            final_detected_point_cam_frame(1) = final_pose_from_plane_vec[j].at<float>(0,1);
            final_detected_point_cam_frame(2) = final_pose_from_plane_vec[j].at<float>(0,2);
            final_detected_point_cam_frame(3) = 1;

            final_detected_point_robot_frame = transformation_mat * final_detected_point_cam_frame;

            Eigen::Vector3f final_pose_of_object_in_robot;

            final_pose_of_object_in_robot(0) = final_detected_point_robot_frame(0);
            final_pose_of_object_in_robot(1) = final_detected_point_robot_frame(1);
            final_pose_of_object_in_robot(2) = final_detected_point_robot_frame(2);

            //pose_vec.push_back(final_pose_of_object_in_robot);
            //do the same above procedure for the normal orientation
            Eigen::Vector4f normal_orientation_cam_frame, normal_orientation_robot_frame;
            normal_orientation_cam_frame.setOnes(), normal_orientation_robot_frame.setOnes();

            normal_orientation_cam_frame(0) = final_pose_from_plane_vec[j].at<float>(0,3);
            normal_orientation_cam_frame(1) = final_pose_from_plane_vec[j].at<float>(0,4);
            normal_orientation_cam_frame(2) = final_pose_from_plane_vec[j].at<float>(0,5);
            normal_orientation_cam_frame(3) = 1;

            normal_orientation_robot_frame = transformation_mat * normal_orientation_cam_frame;

            normal_orientation_cam_frame(3) = final_pose_from_plane_vec[j].at<float>(0,6);

            //this is to calculate the d distance in world frame
            float normal_distance_world_frame = normal_orientation_robot_frame(3) -
                    (final_pose_(0) * normal_orientation_robot_frame(0) +
                     final_pose_(1) * normal_orientation_robot_frame(1) +
                     final_pose_(2) * normal_orientation_robot_frame(2));

            std::cout << "normal coefficients in robot " << normal_orientation_robot_frame << std::endl;
            std::cout << "normal distance in cam "       << final_pose_from_plane_vec[j].at<float>(0,6) << std::endl;
            std::cout << "normal distance in world "     << normal_distance_world_frame << std::endl;
            std::cout << "end " << std::endl;

            //filling the vector for the sending it to the PF
            particle_filter::all_object_info_struct_pf complete_obj_info;

            //if its zeros its horizontal else its vertical
            if(final_pose_from_plane_vec[j].at<float>(0,7) == 0)
                complete_obj_info.plane_type = "horizontal";
            else if(final_pose_from_plane_vec[j].at<float>(0,7) == 1)
                complete_obj_info.plane_type = "vertical";

            complete_obj_info.type                          = object_type;
            complete_obj_info.prob                          = prob;
            complete_obj_info.pose                          = final_pose_of_object_in_robot;
            complete_obj_info.normal_orientation            = normal_orientation_robot_frame;
            //complete_obj_info.normal_orientation            = normal_orientation_cam_frame;
            //complete_obj_info.segmented_point_cloud_plane = segemented_horizontal_plane_from_point_cloud;

            complete_obj_info_vec.push_back(complete_obj_info);
        }
    }

    return complete_obj_info_vec;
}

void semantic_slam_ros::publishParticlePoses()
{
    geometry_msgs::PoseArray particle_poses_vec;
    particle_poses_vec.header.stamp = ros::Time::now();
    particle_poses_vec.header.frame_id = "map";

    for(int i =0; i < all_particles_.size(); ++i)
    {
        geometry_msgs::Pose pose;
        pose.position.x = all_particles_[i].pose(0);
        pose.position.y = all_particles_[i].pose(1);
        pose.position.z = all_particles_[i].pose(2);

        //converting euler to quaternion
        tf::Quaternion quaternion = tf::createQuaternionFromRPY(all_particles_[i].pose(3),all_particles_[i].pose(4),all_particles_[i].pose(5));
        pose.orientation.x = quaternion.getX();
        pose.orientation.y = quaternion.getY();
        pose.orientation.z = quaternion.getZ();
        pose.orientation.w = quaternion.getW();

        particle_poses_vec.poses.push_back(pose);
    }

    particle_poses_pub_.publish(particle_poses_vec);
}

void semantic_slam_ros::publishFinalPose()
{
    geometry_msgs::PoseStamped final_particle_pose;
    ros::Time current_time = ros::Time::now();

    final_particle_pose.header.stamp = current_time;
    final_particle_pose.header.frame_id = "map";

    final_particle_pose.pose.position.x = final_pose_(0) ;
    final_particle_pose.pose.position.y = final_pose_(1);
    final_particle_pose.pose.position.z = final_pose_(2);

    tf::Quaternion quaternion = tf::createQuaternionFromRPY(final_pose_(3),final_pose_(4),final_pose_(5));
    final_particle_pose.pose.orientation.x = quaternion.getX();
    final_particle_pose.pose.orientation.y = quaternion.getY();
    final_particle_pose.pose.orientation.z = quaternion.getZ();
    final_particle_pose.pose.orientation.w = quaternion.getW();

    final_pose_pub_.publish(final_particle_pose);

    nav_msgs::Path final_path;
    final_path.header.stamp = current_time;
    final_path.header.frame_id = "map";

    final_particle_pose_vec_.push_back(final_particle_pose);
    final_path.poses = final_particle_pose_vec_;
    final_path_pub_.publish(final_path);
}

void semantic_slam_ros::publishCorresVOPose()
{
    ros::Time current_time = ros::Time::now();
    geometry_msgs::PoseStamped corres_vo_pose;
    corres_vo_pose.header.stamp = current_time;
    corres_vo_pose.header.frame_id = "map";

    corres_vo_pose.pose.position.x = RVIO_pose_(0);
    corres_vo_pose.pose.position.y = RVIO_pose_(1);
    corres_vo_pose.pose.position.z = RVIO_pose_(2);

    tf::Quaternion quaternion = tf::createQuaternionFromRPY(RVIO_pose_(3),RVIO_pose_(4),RVIO_pose_(5));
    corres_vo_pose.pose.orientation.x = quaternion.getX();
    corres_vo_pose.pose.orientation.y = quaternion.getY();
    corres_vo_pose.pose.orientation.z = quaternion.getZ();
    corres_vo_pose.pose.orientation.w = quaternion.getW();

    corres_vo_pose_pub_.publish(corres_vo_pose);


    nav_msgs::Path vo_path;
    vo_path.header.stamp = current_time;
    vo_path.header.frame_id = "map";

    vo_pose_vec_.push_back(corres_vo_pose);
    vo_path.poses = vo_pose_vec_;
    corres_vo_path_.publish(vo_path);

}

void semantic_slam_ros::publishSegmentedPointCloud(sensor_msgs::PointCloud2 point_cloud_seg)
{
    point_cloud_seg.header.stamp = ros::Time::now();
    point_cloud_seg.header.frame_id = "camera_color_optical_frame";

    segmented_point_cloud_pub_.publish(point_cloud_seg);

}

void semantic_slam_ros::publishFinalDetectedObjectPoint(geometry_msgs::Point final_point)
{
    geometry_msgs::PointStamped final_point_to_pub;
    final_point_to_pub.header.stamp = ros::Time::now();
    final_point_to_pub.header.frame_id = "cam0_optical";

    final_point_to_pub.point.x = final_point.x;
    final_point_to_pub.point.y = final_point.y;
    final_point_to_pub.point.z = final_point.z;

    detected_object_point_pub_.publish(final_point_to_pub);

}

void semantic_slam_ros::publishMappedObjects(std::vector<particle_filter::object_info_struct_pf> mapped_object_vec)
{
    visualization_msgs::MarkerArray marker_arrays;

    for(int i =0; i < mapped_object_vec.size(); ++i)
    {
        visualization_msgs::Marker marker;
        marker.header.stamp = ros::Time();
        marker.header.frame_id = "map";
        marker.ns = "my_namespace";
        marker.pose.position.x = mapped_object_vec[i].pose(0);
        marker.pose.position.y = mapped_object_vec[i].pose(1);
        marker.pose.position.z = mapped_object_vec[i].pose(2);
        marker.pose.orientation.x = 0.0;
        marker.pose.orientation.y = 0.0;
        marker.pose.orientation.z = 0.0;
        marker.pose.orientation.w = 1.0;

        marker.id = i;
        marker.action = visualization_msgs::Marker::ADD;
        marker.type = visualization_msgs::Marker::CUBE;
        marker.scale.x = 0.1;
        marker.scale.y = 0.1;
        marker.scale.z = 0.5;
        marker.color.a = 1.0; // Don't forget to set the alpha!
        marker.color.r = 0.0;
        marker.color.g = 1.0;
        marker.color.b = 0.0;

        marker_arrays.markers.push_back(marker);
    }

    mapped_objects_visualizer_pub_.publish(marker_arrays);
}

void semantic_slam_ros::publishAllMappedObjects(std::vector<particle_filter::all_object_info_struct_pf> mapped_object_vec)
{
    visualization_msgs::MarkerArray marker_arrays;
    int marker_id = 0;

    for(int i =0; i < mapped_object_vec.size(); ++i)
    {
        visualization_msgs::Marker marker;
        marker.header.stamp = ros::Time();
        marker.header.frame_id = "map";
        marker.ns = "my_namespace";
        marker.pose.position.x = mapped_object_vec[i].pose(0);
        marker.pose.position.y = mapped_object_vec[i].pose(1);
        marker.pose.position.z = mapped_object_vec[i].pose(2);
        marker.pose.orientation.x = 0.0;
        marker.pose.orientation.y = 0.0;
        marker.pose.orientation.z = 0.0;
        marker.pose.orientation.w = 1.0;


        if(mapped_object_vec[i].type == "chair")
        {
            if(mapped_object_vec[i].plane_type == "horizontal")
            {
                marker.id = marker_id;
                marker.action = visualization_msgs::Marker::ADD;
                marker.type = visualization_msgs::Marker::CUBE;
                marker.scale.x = 0.3;
                marker.scale.y = 0.3;
                marker.scale.z = 0.01;
                marker.color.a = 1.0; // Don't forget to set the alpha!
                marker.color.r = 0.0;
                marker.color.g = 1.0;
                marker.color.b = 0.0;

            }

            else if(mapped_object_vec[i].plane_type == "vertical")
            {
                marker.id = marker_id;
                marker.action = visualization_msgs::Marker::ADD;
                marker.type = visualization_msgs::Marker::CUBE;
                marker.scale.x = 0.01;
                marker.scale.y = 0.3;
                marker.scale.z = 0.3;
                marker.color.a = 1.0; // Don't forget to set the alpha!
                marker.color.r = 0.0;
                marker.color.g = 1.0;
                marker.color.b = 0.0;

            }
        }

        else if(mapped_object_vec[i].type == "tvmonitor")
        {
            if(mapped_object_vec[i].plane_type == "vertical")
            {
                marker.id = marker_id;
                marker.action = visualization_msgs::Marker::ADD;
                marker.type = visualization_msgs::Marker::CUBE;
                marker.scale.x = 0.01;
                marker.scale.y = 0.3;
                marker.scale.z = 0.3;
                marker.color.a = 1.0; // Don't forget to set the alpha!
                marker.color.r = 1.0;
                marker.color.g = 0.0;
                marker.color.b = 0.0;

            }

        }

        else if (mapped_object_vec[i].type == "laptop")
        {
            if(mapped_object_vec[i].plane_type == "vertical")
            {
                marker.id = marker_id;
                marker.action = visualization_msgs::Marker::ADD;
                marker.type = visualization_msgs::Marker::CUBE;
                marker.scale.x = 0.01;
                marker.scale.y = 0.3;
                marker.scale.z = 0.3;
                marker.color.a = 1.0; // Don't forget to set the alpha!
                marker.color.r = 1.0;
                marker.color.g = 1.0;
                marker.color.b = 0.0;

            }

        }

        else if(mapped_object_vec[i].type == "keyboard")
        {
            if(mapped_object_vec[i].plane_type == "horizontal")
            {
                marker.id = marker_id;
                marker.action = visualization_msgs::Marker::ADD;
                marker.type = visualization_msgs::Marker::CUBE;
                marker.scale.x = 0.3;
                marker.scale.y = 0.3;
                marker.scale.z = 0.01;
                marker.color.a = 1.0; // Don't forget to set the alpha!
                marker.color.r = 1.0;
                marker.color.g = 0.0;
                marker.color.b = 1.0;

            }

        }

        else if(mapped_object_vec[i].type == "book")
        {
            if(mapped_object_vec[i].plane_type == "horizontal")
            {
                marker.id = marker_id;
                marker.action = visualization_msgs::Marker::ADD;
                marker.type = visualization_msgs::Marker::CUBE;
                marker.scale.x = 0.3;
                marker.scale.y = 0.3;
                marker.scale.z = 0.01;
                marker.color.a = 1.0; // Don't forget to set the alpha!
                marker.color.r = 0.0;
                marker.color.g = 0.0;
                marker.color.b = 1.0;

            }

        }

        marker_arrays.markers.push_back(marker);
        marker_id++;
    }

    detected_planes_pub_.publish(marker_arrays);
}

void semantic_slam_ros::publishBestParticleMap(std::vector<particle_filter::particle> all_particles)
{
    visualization_msgs::MarkerArray marker_arrays;
    int marker_id = 0;

    //getting the best particle landmark to plot
    int best_particle_index = this->MaxIndex(all_particles);

    for(int i = 0; i < all_particles[best_particle_index].landmarks.size(); ++i)
    {
        visualization_msgs::Marker marker;
        marker.header.stamp = ros::Time();
        marker.header.frame_id = "map";
        marker.ns = "my_namespace";
        marker.pose.position.x = all_particles[best_particle_index].landmarks[i].mu(0);
        marker.pose.position.y = all_particles[best_particle_index].landmarks[i].mu(1);
        marker.pose.position.z = all_particles[best_particle_index].landmarks[i].mu(2);
        marker.pose.orientation.x = 0.0;
        marker.pose.orientation.y = 0.0;
        marker.pose.orientation.z = 0.0;
        marker.pose.orientation.w = 1.0;

        if(all_particles[best_particle_index].landmarks[i].type == "chair")
        {
            if(all_particles[best_particle_index].landmarks[i].plane_type == "horizontal")
            {
                marker.id = marker_id;
                marker.action = visualization_msgs::Marker::ADD;
                marker.type = visualization_msgs::Marker::CUBE;
                marker.scale.x = 0.3;
                marker.scale.y = 0.3;
                marker.scale.z = 0.01;
                marker.color.a = 1.0; // Don't forget to set the alpha!
                marker.color.r = 0.0;
                marker.color.g = 1.0;
                marker.color.b = 0.0;

            }

            else if(all_particles[best_particle_index].landmarks[i].plane_type == "vertical")
            {
                marker.id = marker_id;
                marker.action = visualization_msgs::Marker::ADD;
                marker.type = visualization_msgs::Marker::CUBE;
                marker.scale.x = 0.01;
                marker.scale.y = 0.3;
                marker.scale.z = 0.3;
                marker.color.a = 1.0; // Don't forget to set the alpha!
                marker.color.r = 0.0;
                marker.color.g = 1.0;
                marker.color.b = 0.0;

            }
        }

        else if(all_particles[best_particle_index].landmarks[i].type == "tvmonitor")
        {
            if(all_particles[best_particle_index].landmarks[i].plane_type == "vertical")
            {
                marker.id = marker_id;
                marker.action = visualization_msgs::Marker::ADD;
                marker.type = visualization_msgs::Marker::CUBE;
                marker.scale.x = 0.01;
                marker.scale.y = 0.3;
                marker.scale.z = 0.3;
                marker.color.a = 1.0; // Don't forget to set the alpha!
                marker.color.r = 1.0;
                marker.color.g = 0.0;
                marker.color.b = 0.0;

            }

        }

        else if (all_particles[best_particle_index].landmarks[i].type == "laptop")
        {
            if(all_particles[best_particle_index].landmarks[i].plane_type == "vertical")
            {
                marker.id = marker_id;
                marker.action = visualization_msgs::Marker::ADD;
                marker.type = visualization_msgs::Marker::CUBE;
                marker.scale.x = 0.01;
                marker.scale.y = 0.3;
                marker.scale.z = 0.3;
                marker.color.a = 1.0; // Don't forget to set the alpha!
                marker.color.r = 1.0;
                marker.color.g = 1.0;
                marker.color.b = 0.0;

            }

        }

        else if(all_particles[best_particle_index].landmarks[i].type == "keyboard")
        {
            if(all_particles[best_particle_index].landmarks[i].plane_type == "horizontal")
            {
                marker.id = marker_id;
                marker.action = visualization_msgs::Marker::ADD;
                marker.type = visualization_msgs::Marker::CUBE;
                marker.scale.x = 0.3;
                marker.scale.y = 0.3;
                marker.scale.z = 0.01;
                marker.color.a = 1.0; // Don't forget to set the alpha!
                marker.color.r = 1.0;
                marker.color.g = 0.0;
                marker.color.b = 1.0;

            }

        }

        else if(all_particles[best_particle_index].landmarks[i].type == "book")
        {
            if(all_particles[best_particle_index].landmarks[i].plane_type == "horizontal")
            {
                marker.id = marker_id;
                marker.action = visualization_msgs::Marker::ADD;
                marker.type = visualization_msgs::Marker::CUBE;
                marker.scale.x = 0.3;
                marker.scale.y = 0.3;
                marker.scale.z = 0.01;
                marker.color.a = 1.0; // Don't forget to set the alpha!
                marker.color.r = 0.0;
                marker.color.g = 0.0;
                marker.color.b = 1.0;

            }

        }

        marker_arrays.markers.push_back(marker);
        marker_id++;
    }

    detected_planes_pub_.publish(marker_arrays);
}

void semantic_slam_ros::publishDetectedPlanes(std::vector<particle_filter::all_object_info_struct_pf> detected_object_vec)
{
    visualization_msgs::MarkerArray marker_arrays;
    int marker_id = 0;

    for(int i =0; i < detected_object_vec.size(); ++i)
    {
        visualization_msgs::Marker marker;
        marker.header.stamp = ros::Time();
        marker.header.frame_id = "map";
        marker.ns = "my_namespace";
        marker.pose.position.x = detected_object_vec[i].pose(0) + final_pose_(0);
        marker.pose.position.y = detected_object_vec[i].pose(1) + final_pose_(1);
        marker.pose.position.z = detected_object_vec[i].pose(2) + final_pose_(2);
        marker.pose.orientation.x = 0.0;
        marker.pose.orientation.y = 0.0;
        marker.pose.orientation.z = 0.0;
        marker.pose.orientation.w = 1.0;

        //std::cout << "Detected Pose " << detected_object_vec[i].pose << std::endl;

        if(detected_object_vec[i].type == "chair")
        {
            if(detected_object_vec[i].plane_type == "horizontal")
            {
                marker.id = marker_id;
                marker.action = visualization_msgs::Marker::ADD;
                marker.type = visualization_msgs::Marker::CUBE;
                marker.scale.x = 0.3;
                marker.scale.y = 0.3;
                marker.scale.z = 0.01;
                marker.color.a = 1.0; // Don't forget to set the alpha!
                marker.color.r = 1.0;
                marker.color.g = 1.0;
                marker.color.b = 1.0;

            }

            else if(detected_object_vec[i].plane_type == "vertical")
            {
                marker.id = marker_id;
                marker.action = visualization_msgs::Marker::ADD;
                marker.type = visualization_msgs::Marker::CUBE;
                marker.scale.x = 0.01;
                marker.scale.y = 0.3;
                marker.scale.z = 0.3;
                marker.color.a = 1.0; // Don't forget to set the alpha!
                marker.color.r = 1.0;
                marker.color.g = 1.0;
                marker.color.b = 1.0;

            }
        }

        else if(detected_object_vec[i].type == "tvmonitor")
        {
            if(detected_object_vec[i].plane_type == "vertical")
            {
                marker.id = marker_id;
                marker.action = visualization_msgs::Marker::ADD;
                marker.type = visualization_msgs::Marker::CUBE;
                marker.scale.x = 0.01;
                marker.scale.y = 0.3;
                marker.scale.z = 0.3;
                marker.color.a = 1.0; // Don't forget to set the alpha!
                marker.color.r = 1.0;
                marker.color.g = 1.0;
                marker.color.b = 1.0;

            }

        }

        else if (detected_object_vec[i].type == "laptop")
        {
            if(detected_object_vec[i].plane_type == "vertical")
            {
                marker.id = marker_id;
                marker.action = visualization_msgs::Marker::ADD;
                marker.type = visualization_msgs::Marker::CUBE;
                marker.scale.x = 0.01;
                marker.scale.y = 0.3;
                marker.scale.z = 0.3;
                marker.color.a = 1.0; // Don't forget to set the alpha!
                marker.color.r = 1.0;
                marker.color.g = 1.0;
                marker.color.b = 1.0;

            }

        }

        else if(detected_object_vec[i].type == "keyboard")
        {
            if(detected_object_vec[i].plane_type == "horizontal")
            {
                marker.id = marker_id;
                marker.action = visualization_msgs::Marker::ADD;
                marker.type = visualization_msgs::Marker::CUBE;
                marker.scale.x = 0.3;
                marker.scale.y = 0.3;
                marker.scale.z = 0.01;
                marker.color.a = 1.0; // Don't forget to set the alpha!
                marker.color.r = 1.0;
                marker.color.g = 1.0;
                marker.color.b = 1.0;

            }

        }

        else if(detected_object_vec[i].type == "book")
        {
            if(detected_object_vec[i].plane_type == "horizontal")
            {
                marker.id = marker_id;
                marker.action = visualization_msgs::Marker::ADD;
                marker.type = visualization_msgs::Marker::CUBE;
                marker.scale.x = 0.3;
                marker.scale.y = 0.3;
                marker.scale.z = 0.01;
                marker.color.a = 1.0; // Don't forget to set the alpha!
                marker.color.r = 1.0;
                marker.color.g = 1.0;
                marker.color.b = 1.0;

            }

        }

        marker_arrays.markers.push_back(marker);
        marker_id++;
    }

    mapped_objects_visualizer_pub_.publish(marker_arrays);

}


void semantic_slam_ros::publishDetectedPlanesPointCloud(std::vector<particle_filter::all_object_info_struct_pf> detected_object_vec)
{
    pcl::PointCloud<pcl::PointXYZRGB> point_cloud_pcl;

    for(int i =0; i < detected_object_vec.size(); ++i)
    {
        for(int j=0; j < detected_object_vec[i].segmented_point_cloud_plane->points.size(); ++j)
        {
            point_cloud_pcl.points.push_back(detected_object_vec[i].segmented_point_cloud_plane->points[j]);
        }
    }

    sensor_msgs::PointCloud2 point_cloud;
    pcl::toROSMsg(point_cloud_pcl, point_cloud);
    point_cloud.header.stamp = ros::Time::now();
    point_cloud.header.frame_id = "cam";
    detected_planes_point_cloud_pub_.publish(point_cloud);
}


void semantic_slam_ros::publishNewMappedObjects(std::vector<particle_filter::object_info_struct_all_points_pf> mapped_object_vec)
{
    std::vector<particle_filter::particle> all_particles;
    all_particles = particle_filter_obj_.getAllParticles();
    int best_particle_index = this->MaxIndex(all_particles);

    sensor_msgs::PointCloud2 point_cloud;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr point_cloud_pcl (new pcl::PointCloud<pcl::PointXYZRGB>);

    this->getPointCloudData(point_cloud);

    pcl::fromROSMsg(point_cloud, *point_cloud_pcl);
    point_cloud_pcl = plane_segmentation_obj_.preprocessPointCloud(point_cloud_pcl);

    Eigen::Matrix4f transformation_mat;
    semantic_tools_obj_.transformNormalsToWorld(final_pose_,
                                                transformation_mat,
                                                0);

    //convert the points to world frame using transformation mat
    for(int j = 0; j < num_particles_; ++j)
    {
        for(size_t i = 0; i < point_cloud_pcl->size(); ++i)
        {
            Eigen::Vector4f final_detected_point_cam_frame, final_detected_point_robot_frame;
            final_detected_point_cam_frame.setOnes(), final_detected_point_robot_frame.setOnes();

            final_detected_point_cam_frame(0) = point_cloud_pcl->points[i].x;
            final_detected_point_cam_frame(1) = point_cloud_pcl->points[i].y;
            final_detected_point_cam_frame(2) = point_cloud_pcl->points[i].z;

            final_detected_point_robot_frame = transformation_mat * final_detected_point_cam_frame;

            //fill the point cloud pcl with the new tranformed points
            pcl::PointXYZRGB  current_point_cloud_pcl;
            current_point_cloud_pcl.x   = final_detected_point_robot_frame(0) + all_particles[j].pose(0);
            current_point_cloud_pcl.y   = final_detected_point_robot_frame(1) + all_particles[j].pose(1);
            current_point_cloud_pcl.z   = final_detected_point_robot_frame(2) + all_particles[j].pose(2);
            current_point_cloud_pcl.rgb = point_cloud_pcl->points[i].rgb;

            mapped_point_cloud_pcl_[j].points.push_back(current_point_cloud_pcl);
        }

    }

    sensor_msgs::PointCloud2 final_point_cloud;
    pcl::toROSMsg(mapped_point_cloud_pcl_[best_particle_index], final_point_cloud);
    final_point_cloud.header.stamp = ros::Time::now();
    final_point_cloud.header.frame_id = "map";
    mapped_points_pub_.publish(final_point_cloud);
}

void semantic_slam_ros::publishGroundTruthPoints(std::vector<geometry_msgs::Point> points)
{
    visualization_msgs::MarkerArray marker_arrays;

    for(int i =0; i < points.size(); ++i)
    {
        visualization_msgs::Marker marker;
        marker.header.stamp = ros::Time();
        marker.header.frame_id = "map";
        marker.ns = "my_ground_namespace";
        marker.pose.position.x = points[i].x;
        marker.pose.position.y = points[i].y;
        marker.pose.position.z = 0.0;
        marker.pose.orientation.x = 0.0;
        marker.pose.orientation.y = 0.0;
        marker.pose.orientation.z = 0.0;
        marker.pose.orientation.w = 1.0;

        marker.id = i;
        marker.action = visualization_msgs::Marker::ADD;
        marker.type = visualization_msgs::Marker::SPHERE;
        marker.scale.x = 0.3;
        marker.scale.y = 0.3;
        marker.scale.z = 0.3;
        marker.color.a = 1.0; // Don't forget to set the alpha!
        marker.color.r = 1.0;
        marker.color.g = 0.0;
        marker.color.b = 0.0;

        marker_arrays.markers.push_back(marker);
    }

    ground_truth_points_pub_.publish(marker_arrays);

}

//static methods
int semantic_slam_ros::MaxIndex(std::vector<particle_filter::particle> particles)
{
    int max_Index = 0;
    double max_val = 0.0;

    for (int i = 1; i < particles.size(); ++i)
    {
        if (max_val < particles[i].weight)
        {
            max_val = particles[i].weight;
            max_Index = i;
        }
    }

    return max_Index;
}
