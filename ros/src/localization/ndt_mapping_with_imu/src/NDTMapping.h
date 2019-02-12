#ifndef NDT_MAPPING_H
#define NDT_MAPPING_H

#include <ros/ros.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <pthread.h>

#include <nav_msgs/Odometry.h>
#include <sensor_msgs/Imu.h>
#include <sensor_msgs/PointCloud2.h>
#include <std_msgs/Float32.h>
#include <std_msgs/Bool.h>

#include <tf/transform_broadcaster.h>
#include <tf/transform_listener.h>
#include <tf/transform_datatypes.h>

#include <pcl/io/io.h>
#include <pcl/io/pcd_io.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/registration/ndt.h>
#include <ctime>
#ifdef CUDA_FOUND
#include <ndt_gpu/NormalDistributionsTransform.h>
#endif



namespace NDT_MAPPING{
    using PointI = pcl::PointXYZI;
    //定义pose这个结构，除了xyzrpy，还有由此得到的矩阵
    struct pose{
        double x;
		double y;
		double z;
		double roll;
		double pitch;
		double yaw;

		void init(){
			x = y = z = 0.0;
			roll = pitch = yaw = 0.0;
		}
        // TODO: 看一下Eigen库中这几个函数
        Eigen::Matrix4d rotateRPY(){
			Eigen::Translation3d tf_trans(x,y,z);
			Eigen::AngleAxisd rot_x(roll,Eigen::Vector3d::UnitX());
			Eigen::AngleAxisd rot_y(pitch,Eigen::Vector3d::UnitY());
			Eigen::AngleAxisd rot_z(yaw,Eigen::Vector3d::UnitZ());
			Eigen::Matrix4d mat=(tf_trans*rot_z*rot_y*rot_x).matrix();
			return mat;
		}
    };
    //定义使用flag，我只使用cpu, gpu和pcl自带的ndt，pcl自带的ndt非常慢
    enum class MethodType{
		use_pcl = 0,
		use_cpu = 1,
		use_gpu = 2,
		use_omp = 3,
		use_gpu_ptr = 4,
	};
    static MethodType _method_type = MethodType::use_cpu; // 默认使用cpu

    class NDTMapping{
    private: //数据放在private
        //Transform
        tf::TransformBroadcaster tf_broadcaster;
		tf::TransformListener tf_listener;
        double param_tf_timeout;
        std::string param_base_frame;
		std::string param_laser_frame;
        Eigen::Matrix4f tf_btol, tf_ltob;

        pose previous_pose,guess_pose,guess_pose_imu,guess_pose_odom,guess_pose_imu_odom;
		pose current_pose,current_pose_imu,current_pose_odom,current_pose_imu_odom;
		pose ndt_pose,localizer_pose;
        pose added_pose;


		ros::Time current_scan_time;
		ros::Time previous_scan_time;
		ros::Duration scan_duration;


        // 定义Publisher
		ros::Publisher debug_map_pub;
		ros::Publisher matching_map_pub;        // 地图发布
		ros::Publisher refiltered_map_pub;
		ros::Publisher current_pose_pub;   // 位置发布
		ros::Subscriber points_sub;
		ros::Subscriber imu_sub;
		ros::Subscriber odom_sub;
		geometry_msgs::PoseStamped current_pose_msg, guess_pose_msg;
		
		MethodType _method_type;

		// 设置变量,用以接受ndt配准后的参数
		double fitness_score;
		bool has_converged;
		int final_num_iteration;
		double transformation_probability;
		// 设置变量,用以接受imu和odom消息
		sensor_msgs::Imu imu;
		nav_msgs::Odometry odom;

		// 定义各种差异值(两次采集数据之间的差异,包括点云位置差异,imu差异,odom差异,imu-odom差异)
		double diff;
		double diff_x, diff_y, diff_z, diff_yaw;  // current_pose - previous_pose // 定义两帧点云差异值 --以确定是否更新点云等
		double offset_imu_x, offset_imu_y, offset_imu_z, offset_imu_roll, offset_imu_pitch, offset_imu_yaw;
		double offset_odom_x, offset_odom_y, offset_odom_z, offset_odom_roll, offset_odom_pitch, offset_odom_yaw;
		double offset_imu_odom_x, offset_imu_odom_y, offset_imu_odom_z, offset_imu_odom_roll, offset_imu_odom_pitch,
				offset_imu_odom_yaw;

		// 定义速度值 --包括实际速度值,和imu取到的速度值
		double current_velocity_x;
		double current_velocity_y;
		double current_velocity_z;

		double current_velocity_imu_x;
		double current_velocity_imu_y;
		double current_velocity_imu_z;

		double param_global_voxel_leafsize;
		double param_min_update_target_map;
		double param_extract_length;
		double param_extract_width;
		bool param_visualize;

		pcl::PointCloud<pcl::PointXYZI> map;  // 此处定义地图  --用于ndt匹配
		pcl::PointCloud<pcl::PointXYZI> global_map;  // 此处为构建打全局地图

		pcl::NormalDistributionsTransform<pcl::PointXYZI, pcl::PointXYZI> pcl_ndt;
		// cpu::NormalDistributionsTransform<pcl::PointXYZI, pcl::PointXYZI> cpu_ndt;  // cpu方式  --可以直接调用吗??可以
		#ifdef CUDA_FOUND
			gpu::GNormalDistributionsTransform gpu_ndt;
			// TODO:此处增加共享内存方式的gpu_ndt_ptr
			// std::shared_ptr<gpu::GNormalDistributionsTransform> gpu_ndt = std::make_shared<gpu::GNormalDistributionsTransform>();
		#endif


		// Default values  // 公共ndt参数设置
		int max_iter;        // Maximum iterations
		float ndt_res;      // Resolution
		double step_size;   // Step size
		double trans_eps;  // Transformation epsilon

		// Leaf size of VoxelGrid filter.   ---该处定义的是一个默认的栅格大小
		double voxel_leaf_size;

		Eigen::Matrix4f gnss_transform;  // 保存GPS信号的变量

		double min_scan_range;  // min和max用于第一次进行点云过滤(截取两个同心圆内的)
		double max_scan_range;
		double min_add_scan_shift;  // 定义将点云添加到locaMap里的最小间隔值  --应该是添加到localMap吧??

		int initial_scan_loaded;

		// 重要变量参数 :: 用以指示是否使用imu,是否使用odom
		bool _use_imu;
		bool _use_odom;
		bool _imu_upside_down;  // 用以解决坐标系方向(正负变换)问题 (比如x变更为-x等)


		std::string _imu_topic;  // 定义imu消息的topic
		std::string _odom_topic;
		std::string _lidar_topic;
	
	public:
		NDTMapping(){

			_method_type = MethodType::use_gpu;

			diff = 0.0;transformed_scan_ptr
			diff_x = ditransformed_scan_ptr_y = diff_z = 0.0;
			// 定义速度transformed_scan_ptr --包括实际速度值,和imu取到的速度值
			current_velocity_x = 0.0;
			current_velocity_y = 0.0;
			current_velocity_z = 0.0;
			current_velocity_imu_x = 0.0;
			current_velocity_imu_y = 0.0;
			current_velocity_imu_z = 0.0;

			_use_imu = false;
			_use_odom = false;
			_imu_upside_down = false;  // 用以解决坐标系方向(正负变换)问题 (比如x变更为-x等)

			initial_scan_loaded = 0;  // 用以确定是否为第一帧点云(第一帧点云不做匹配,直接添加到地图中去)
		}

		void param_initial(ros::NodeHandle &nh,ros::NodeHandle &private_handle);

		void imu_odom_calc(ros::Time current_time);
		void imu_calc(ros::Time current_time);
		void odom_calc(ros::Time current_time);

		double warpToPm(double a_num,const double a_max);
		double warpToPmPi(double a_angle_rad);
		void imuUpSideDown(const sensor_msgs::Imu::Ptr input);
		double calcDiffForRadian(const double lhs_rad,const double rhs_rad);

		void imu_callback(const sensor_msgs::Imu::Ptr& input);
		void odom_callback(const nav_msgs::Odometry::ConstPtr& input);

		void points_callback(const sensor_msgs::PointCloud2::ConstPtr& input);

		void run(ros::NodeHandle &nh,ros::NodeHandle &private_nh);
    };

}

#endif #NDT_MAPPING_H