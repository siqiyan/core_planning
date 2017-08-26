/*
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  * Neither the name of Autoware nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "dp_planner_core.h"

#include <visualization_msgs/MarkerArray.h>
#include "geo_pos_conv.hh"


#include <pcl_conversions/pcl_conversions.h>
#include <pcl/io/io.h>
#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>
#include "UtilityH.h"
#include "MatrixOperations.h"

namespace PlannerXNS
{

PlannerX::PlannerX()
{

	clock_gettime(0, &m_Timer);
	m_counter = 0;
	m_frequency = 0;

	m_nTrackObjects = 0;
	m_nContourPoints = 0;
	m_nOriginalPoints = 0;
	m_TrackingTime = 0;
	bInitPos = false;
	bNewCurrentPos = false;
	bNewClusters = false;
	bNewBoxes = false;
	bVehicleState = false;
	bNewEmergency = false;
	m_bEmergencyStop = 0;
	bNewTrafficLigh = false;
	m_bGreenLight = false; UtilityHNS::UtilityH::GetTickCount(m_TrafficLightTimer);
	bNewOutsideControl = false;
	m_bOutsideControl = 0;
	bNewAStarPath = false;
	UtilityHNS::UtilityH::GetTickCount(m_AStartPlanningTimer);
	bWayPlannerPath = false;
	bKmlMapLoaded = false;
	m_bEnableTracking = true;
	m_ObstacleTracking.m_MAX_ASSOCIATION_DISTANCE = 2.0;
	m_ObstacleTracking.m_MAX_TRACKS_AFTER_LOSING = 5;
	m_ObstacleTracking.m_DT = 0.12;
	m_ObstacleTracking.m_bUseCenterOnly = true;

	enablePlannerDynamicSwitch = false;


	int iSource = 0;
	nh.getParam("/dp_planner/mapSource", iSource);
	if(iSource == 0)
		m_MapSource = MAP_AUTOWARE;
	else if (iSource == 1)
		m_MapSource = MAP_FOLDER;
	else if(iSource == 2)
		m_MapSource = MAP_KML_FILE;

	nh.getParam("/dp_planner/mapFileName", m_KmlMapPath);

	UpdatePlanningParams();

	tf::StampedTransform transform;
	RosHelpers::GetTransformFromTF("map", "world", transform);
	m_OriginPos.position.x  = transform.getOrigin().x();
	m_OriginPos.position.y  = transform.getOrigin().y();
	m_OriginPos.position.z  = transform.getOrigin().z();


	std::string topic_prefix;
	nh.getParam("/dp_planner/enablePlannerDynamicSwitch", enablePlannerDynamicSwitch);
	if(enablePlannerDynamicSwitch){
		topic_prefix = "/dp";
		pub_LocalTrajectoriesRviz_dynamic = nh.advertise<visualization_msgs::MarkerArray>("local_trajectories_dynamic", 1);
		pub_EnableLattice = nh.advertise<std_msgs::Int32>("enableLattice", 1);
	}

	pub_LocalPath = nh.advertise<autoware_msgs::lane>(topic_prefix + "/final_waypoints", 100,true);
	pub_LocalBasePath = nh.advertise<autoware_msgs::lane>(topic_prefix + "/base_waypoints", 100,true);
	pub_ClosestIndex = nh.advertise<std_msgs::Int32>(topic_prefix + "/closest_waypoint", 100,true);

	pub_BehaviorState = nh.advertise<geometry_msgs::TwistStamped>("current_behavior", 1);
	pub_GlobalPlanNodes = nh.advertise<geometry_msgs::PoseArray>("global_plan_nodes", 1);
	pub_StartPoint = nh.advertise<geometry_msgs::PoseWithCovarianceStamped>("GlobalStartpose", 1);
	pub_GoalPoint = nh.advertise<geometry_msgs::PoseStamped>("GlobalGoalPose", 1);
	pub_AStarStartPoint = nh.advertise<geometry_msgs::PoseStamped>("global_plan_start", 1);
	pub_AStarGoalPoint = nh.advertise<geometry_msgs::PoseStamped>("global_plan_goal", 1);

	pub_DetectedPolygonsRviz = nh.advertise<visualization_msgs::MarkerArray>("detected_polygons", 1, true);
	pub_TrackedObstaclesRviz = nh.advertise<jsk_recognition_msgs::BoundingBoxArray>("dp_planner_tracked_boxes", 1);
	pub_LocalTrajectoriesRviz = nh.advertise<visualization_msgs::MarkerArray>("local_trajectories", 1);
	
	pub_TestLineRviz	= nh.advertise<visualization_msgs::MarkerArray>("testing_line", 1);
	pub_BehaviorStateRviz = nh.advertise<visualization_msgs::Marker>("behavior_state", 1);
	pub_SafetyBorderRviz  = nh.advertise<visualization_msgs::Marker>("safety_border", 1);
	pub_cluster_cloud = nh.advertise<sensor_msgs::PointCloud2>("simu_points_cluster",1);
	pub_SimuBoxPose	  = nh.advertise<geometry_msgs::PoseArray>("sim_box_pose_ego", 100);


	sub_initialpose 	= nh.subscribe("/initialpose", 				1,		&PlannerX::callbackGetInitPose, 		this);
	sub_current_pose 	= nh.subscribe("/current_pose", 			1,		&PlannerX::callbackGetCurrentPose, 		this);
	sub_cluster_cloud 	= nh.subscribe("/cloud_clusters",			1,		&PlannerX::callbackGetCloudClusters, 	this);
	sub_bounding_boxs  	= nh.subscribe("/bounding_boxes",			1,		&PlannerX::callbackGetBoundingBoxes, 	this);
	sub_WayPlannerPaths = nh.subscribe("/realtime_cost_map",		1,		&PlannerX::callbackGetCostMap, 	this);

#ifdef DATASET_GENERATION_BLOCK

	m_iRecordNumber = 0;
//	tf::StampedTransform base_transform;
//	int nFailedCounter = 0;
//	while (1)
//	{
//		try
//		{
//			m_Transformation.lookupTransform("base_link", "world", ros::Time(0), base_transform);
//			break;
//		}
//		catch (tf::TransformException& ex)
//		{
//			if(nFailedCounter > 2)
//			{
//				ROS_ERROR("%s", ex.what());
//			}
//			ros::Duration(1.0).sleep();
//			nFailedCounter ++;
//		}
//	}

	m_ImagesVectors.open("/home/user/data/db/input.csv");
	m_TrajVectors.open("/home/user/data/db/output.csv");

	sub_image_reader = nh.subscribe("/image_raw", 1, &PlannerX::callbackReadImage, 		this);
#endif

	/**
	 * @todo This works only in simulation (Autoware or ff_Waypoint_follower), twist_cmd should be changed, consult team
	 */
	int bVelSource = 1;
	nh.getParam("/dp_planner/enableOdometryStatus", bVelSource);
	if(bVelSource == 0)
		sub_robot_odom 			= nh.subscribe("/odom", 					100,	&PlannerX::callbackGetRobotOdom, 	this);
	else if(bVelSource == 1)
		sub_current_velocity 	= nh.subscribe("/current_velocity",		100,	&PlannerX::callbackGetVehicleStatus, 	this);
	else if(bVelSource == 2)
		sub_can_info 			= nh.subscribe("/can_info",		100,	&PlannerX::callbackGetCanInfo, 	this);



	sub_EmergencyStop 	= nh.subscribe("/emergency_stop_signal", 	100,	&PlannerX::callbackGetEmergencyStop, 	this);
	sub_TrafficLight 	= nh.subscribe("/traffic_signal_info", 		10,		&PlannerX::callbackGetTrafficLight, 	this);

	if(m_bEnableOutsideControl)
		sub_OutsideControl 	= nh.subscribe("/usb_controller_r_signal", 	10,		&PlannerX::callbackGetOutsideControl, 	this);
	else
		m_bOutsideControl = 1;

	sub_AStarPath 		= nh.subscribe("/astar_path", 				10,		&PlannerX::callbackGetAStarPath, 		this);
	sub_WayPlannerPaths = nh.subscribe("/lane_waypoints_array", 	1,		&PlannerX::callbackGetWayPlannerPath, 	this);

	if(m_MapSource == MAP_AUTOWARE)
	{
		sub_map_points 	= nh.subscribe("/vector_map_info/point", 		1, &PlannerX::callbackGetVMPoints, 		this);
		sub_map_lanes 	= nh.subscribe("/vector_map_info/lane", 		1, &PlannerX::callbackGetVMLanes, 		this);
		sub_map_nodes 	= nh.subscribe("/vector_map_info/node", 		1, &PlannerX::callbackGetVMNodes, 		this);
		sup_stop_lines 	= nh.subscribe("/vector_map_info/stop_line",	1, &PlannerX::callbackGetVMStopLines, 	this);
		sub_dtlanes 	= nh.subscribe("/vector_map_info/dtlane", 		1, &PlannerX::callbackGetVMCenterLines,	this);
	}

	sub_simulated_obstacle_pose_rviz = nh.subscribe("/clicked_point", 		1, &PlannerX::callbackGetRvizPoint,	this);
}

PlannerX::~PlannerX()
{
#ifdef OPENPLANNER_ENABLE_LOGS
	UtilityHNS::DataRW::WriteLogData(UtilityHNS::UtilityH::GetHomeDirectory()+UtilityHNS::DataRW::LoggingMainfolderName+UtilityHNS::DataRW::StatesLogFolderName, "MainLog",
			"time,dt,Behavior State,behavior,num_Tracked_Objects,num_Cluster_Points,num_Contour_Points,t_Tracking,t_Calc_Cost, t_Behavior_Gen, t_Roll_Out_Gen, num_RollOuts, Full_Block, idx_Central_traj, iTrajectory, Stop Sign, Traffic Light, Min_Stop_Distance, follow_distance, follow_velocity, Velocity, Steering, X, Y, Z, heading,"
			, m_LogData);
#endif

#ifdef DATASET_GENERATION_BLOCK
	m_ImagesVectors.close();
	m_TrajVectors.close();
#endif
}

#ifdef DATASET_GENERATION_BLOCK
void PlannerX::callbackReadImage(const sensor_msgs::ImageConstPtr& img)
{
	//std::cout << "Reading Image Data ... " << std::endl;
	cv_bridge::CvImagePtr cv_image = cv_bridge::toCvCopy(img, sensor_msgs::image_encodings::BGR8);
	m_CurrImage = cv_image->image;

}

void PlannerX::ExtractPathFromDriveData(double max_extraction)
{
	double d = 0;
	DataPairs dp;

	for(int i = m_DrivePoints.size()-1; i >= 0 ; i--)
	{
		d += m_DrivePoints.at(i).currentPos.cost;

		if(d >= 30)
		{
			dp.image =  m_DrivePoints.at(i).image.clone();
			dp.currentPos =  m_DrivePoints.at(i).currentPos;
			dp.vehicleState = m_DrivePoints.at(i).vehicleState;
			cv::Mat gray_image;
			cvtColor( dp.image, gray_image, cv::COLOR_BGR2GRAY);
			cv::Rect roi;
			roi.x = 0;
			roi.y = gray_image.rows/3;
			roi.width = gray_image.cols;
			roi.height = gray_image.rows - gray_image.rows/3;
			cv::Mat halfImg = gray_image(roi);

			std::ostringstream image_name;
			image_name << "/home/user/data/db/vis/image_" << m_iRecordNumber << ".png";
			std::ostringstream half_image_name;
			half_image_name <<  "/home/user/data/db/img/img_"  << m_iRecordNumber << ".png";
			std::ostringstream label_name ;
			label_name << "/home/user/data/db/csv/lbl_" << m_iRecordNumber << ".csv";


			PlannerHNS::Mat3 rotationMat(-dp.currentPos.pos.a);
			PlannerHNS::Mat3 translationMat(-dp.currentPos.pos.x, -dp.currentPos.pos.y);
			for(unsigned int ip=0; ip < dp.path.size(); ip++)
			{
				dp.path.at(ip).pos = translationMat*dp.path.at(ip).pos;
				dp.path.at(ip).pos = rotationMat*dp.path.at(ip).pos;
			}

			PlannerHNS::PlanningHelpers::SmoothPath(dp.path, 0.45, 0.3, 0.01);
			PlannerHNS::PlanningHelpers::FixPathDensity(dp.path, d/20.0);
			PlannerHNS::PlanningHelpers::SmoothPath(dp.path, 0.45, 0.35, 0.01);

			for(unsigned int ip=0; ip <dp.path.size()-1; ip++)
			{
				cv::Point p1;
				p1.y = dp.image.rows - (fabs(dp.path.at(ip).pos.x) *  halfImg.rows  / 40.0);
				p1.x = halfImg.cols/2 + (-dp.path.at(ip).pos.y * halfImg.cols / 20.0);

				cv::Point p2;
				p2.y = dp.image.rows - (fabs(dp.path.at(ip+1).pos.x) *  halfImg.rows  / 40.0);
				p2.x = halfImg.cols/2 + (-dp.path.at(ip+1).pos.y * halfImg.cols / 20.0);

				cv::line(dp.image, p1, p2,  cv::Scalar( 0, 255, 0 ), 2, 8);
			}

			WritePathCSV(label_name.str(), dp.path);
			imwrite(image_name.str(),  dp.image);
			imwrite(half_image_name.str(),  halfImg);

			WriteImageAndPathCSV(halfImg.clone(), dp.path);

			for(int j = 0; j <= i; j++)
				if(m_DrivePoints.size() > 0)
					m_DrivePoints.erase(m_DrivePoints.begin()+0);

			m_iRecordNumber++;
			std::cout << "Extract Data: " << "Cost: " << d << ", Index: " << i << ", Path Size: " << dp.path.size() << std::endl;
			return;
		}

		dp.path.insert(dp.path.begin(), m_DrivePoints.at(i).currentPos);
	}
}

void PlannerX::WriteImageAndPathCSV(cv::Mat img,std::vector<PlannerHNS::WayPoint>& path)
{
	if(m_ImagesVectors.is_open())
	{
		ostringstream str_img;
		str_img.precision(0);
		for(int c=0; c<img.cols; c++)
		{
			for(int r=0; r<img.rows; r++)
			{
				short x = img.at<uchar>(r, c);
				str_img << x << ',';
			}
		}

		m_ImagesVectors << str_img.str() << "\r\n";
	}

	if(m_TrajVectors.is_open())
	{
		ostringstream strwp;
		 for(unsigned int i=0; i<path.size(); i++)
		 {
			 strwp << path.at(i).pos.x<<","<< path.at(i).pos.y << ",";
		 }
		m_TrajVectors << strwp.str() << "\r\n";
	}
}

void PlannerX::WritePathCSV(const std::string& fName, std::vector<PlannerHNS::WayPoint>& path)
{
	vector<string> dataList;
	 for(unsigned int i=0; i<path.size(); i++)
	 {
		 ostringstream strwp;
		 strwp << path.at(i).pos.x<<","<< path.at(i).pos.y <<","<< path.at(i).v << ",";
		 dataList.push_back(strwp.str());
	 }

	 std::ofstream f(fName.c_str());

	if(f.is_open())
	{
		for(unsigned int i = 0 ; i < dataList.size(); i++)
			f << dataList.at(i) << "\r\n";
	}

	f.close();
}

#endif

void PlannerX::callbackGetVMPoints(const vector_map_msgs::PointArray& msg)
{
	ROS_INFO("Received Map Points");
	m_AwMap.points = msg;
	m_AwMap.bPoints = true;
}

void PlannerX::callbackGetVMLanes(const vector_map_msgs::LaneArray& msg)
{
	ROS_INFO("Received Map Lane Array");
	m_AwMap.lanes = msg;
	m_AwMap.bLanes = true;
}

void PlannerX::callbackGetVMNodes(const vector_map_msgs::NodeArray& msg)
{
}

void PlannerX::callbackGetVMStopLines(const vector_map_msgs::StopLineArray& msg)
{
}

void PlannerX::callbackGetVMCenterLines(const vector_map_msgs::DTLaneArray& msg)
{
	ROS_INFO("Received Map Center Lines");
	m_AwMap.dtlanes = msg;
	m_AwMap.bDtLanes = true;
}

void PlannerX::UpdatePlanningParams()
{
	PlannerHNS::PlanningParams params;

	nh.getParam("/dp_planner/enableSwerving", params.enableSwerving);
	if(params.enableSwerving)
		params.enableFollowing = true;
	else
		nh.getParam("/dp_planner/enableFollowing", params.enableFollowing);

	nh.getParam("/dp_planner/enableHeadingSmoothing", params.enableHeadingSmoothing);
	nh.getParam("/dp_planner/enableTrafficLightBehavior", params.enableTrafficLightBehavior);
	nh.getParam("/dp_planner/enableStopSignBehavior", params.enableStopSignBehavior);

	nh.getParam("/dp_planner/maxVelocity", params.maxSpeed);
	nh.getParam("/dp_planner/minVelocity", params.minSpeed);
	nh.getParam("/dp_planner/maxLocalPlanDistance", params.microPlanDistance);
	nh.getParam("/dp_planner/samplingTipMargin", params.carTipMargin);
	nh.getParam("/dp_planner/samplingOutMargin", params.rollInMargin);
	nh.getParam("/dp_planner/samplingSpeedFactor", params.rollInSpeedFactor);

	nh.getParam("/dp_planner/pathDensity", params.pathDensity);
	nh.getParam("/dp_planner/rollOutDensity", params.rollOutDensity);
	if(params.enableSwerving)
		nh.getParam("/dp_planner/rollOutsNumber", params.rollOutNumber);
	else
		params.rollOutNumber = 0;

	nh.getParam("/dp_planner/horizonDistance", params.horizonDistance);
	nh.getParam("/dp_planner/minFollowingDistance", params.minFollowingDistance);
	nh.getParam("/dp_planner/minDistanceToAvoid", params.minDistanceToAvoid);
	nh.getParam("/dp_planner/maxDistanceToAvoid", params.maxDistanceToAvoid);
	nh.getParam("/dp_planner/speedProfileFactor", params.speedProfileFactor);

	nh.getParam("/dp_planner/horizontalSafetyDistance", params.horizontalSafetyDistancel);
	nh.getParam("/dp_planner/verticalSafetyDistance", params.verticalSafetyDistance);

	nh.getParam("/dp_planner/enableLaneChange", params.enableLaneChange);
	nh.getParam("/dp_planner/enabTrajectoryVelocities", params.enabTrajectoryVelocities);

	nh.getParam("/dp_planner/enableObjectTracking", m_bEnableTracking);
	nh.getParam("/dp_planner/enableOutsideControl", m_bEnableOutsideControl);


	PlannerHNS::ControllerParams controlParams;
	controlParams.Steering_Gain = PlannerHNS::PID_CONST(0.07, 0.02, 0.01);
	controlParams.Velocity_Gain = PlannerHNS::PID_CONST(0.1, 0.005, 0.1);
	nh.getParam("/dp_planner/steeringDelay", controlParams.SteeringDelay);
	nh.getParam("/dp_planner/minPursuiteDistance", controlParams.minPursuiteDistance );

	PlannerHNS::CAR_BASIC_INFO vehicleInfo;

	nh.getParam("/dp_planner/width", vehicleInfo.width);
	nh.getParam("/dp_planner/length", vehicleInfo.length);
	nh.getParam("/dp_planner/wheelBaseLength", vehicleInfo.wheel_base);
	nh.getParam("/dp_planner/turningRadius", vehicleInfo.turning_radius);
	nh.getParam("/dp_planner/maxSteerAngle", vehicleInfo.max_steer_angle);
	vehicleInfo.max_speed_forward = params.maxSpeed;
	vehicleInfo.min_speed_forward = params.minSpeed;

	m_LocalPlanner.m_SimulationSteeringDelayFactor = controlParams.SimulationSteeringDelay;
	m_LocalPlanner.Init(controlParams, params, vehicleInfo);
	m_LocalPlanner.m_pCurrentBehaviorState->m_Behavior = PlannerHNS::INITIAL_STATE;

}

void PlannerX::callbackGetInitPose(const geometry_msgs::PoseWithCovarianceStampedConstPtr &msg)
{
	if(!bInitPos)
	{
		PlannerHNS::WayPoint p;
		ROS_INFO("init Simulation Rviz Pose Data: x=%f, y=%f, z=%f, freq=%d", msg->pose.pose.position.x, msg->pose.pose.position.y, msg->pose.pose.position.z, m_frequency);
		m_InitPos = PlannerHNS::WayPoint(msg->pose.pose.position.x+m_OriginPos.position.x,
				msg->pose.pose.position.y+m_OriginPos.position.y,
				msg->pose.pose.position.z+m_OriginPos.position.z,
				tf::getYaw(msg->pose.pose.orientation));
		m_CurrentPos = m_InitPos;
		bInitPos = true;
	}
}

void PlannerX::callbackGetCostMap(const nav_msgs::OccupancyGrid& msg)
{
}

void PlannerX::callbackGetRvizPoint(const geometry_msgs::PointStampedConstPtr& msg)
{
	//Add Simulated Obstacle polygon
	timespec t;
	UtilityHNS::UtilityH::GetTickCount(t);
	srand(t.tv_nsec);
	double width = SIMU_OBSTACLE_WIDTH;//((double)(rand()%10)/10.0) * 1.5 + 0.25;
	double length = SIMU_OBSTACLE_LENGTH;//((double)(rand()%10)/10.0) * 0.5 + 0.25;
	double height = SIMU_OBSTACLE_HEIGHT;

	geometry_msgs::PointStamped point;
	point.point.x = msg->point.x+m_OriginPos.position.x;
	point.point.y = msg->point.y+m_OriginPos.position.y;
	point.point.z = msg->point.z+m_OriginPos.position.z;

	autoware_msgs::CloudClusterArray clusters_array;
	clusters_array.clusters.push_back(GenerateSimulatedObstacleCluster(width, length, height, 50, point));
	m_OriginalClusters.clear();
	int nNum1, nNum2;
	RosHelpers::ConvertFromAutowareCloudClusterObstaclesToPlannerH(m_CurrentPos, m_LocalPlanner.m_CarInfo, clusters_array, m_OriginalClusters, nNum1, nNum2);
	m_TrackedClusters = m_OriginalClusters;

	pcl::PointCloud<pcl::PointXYZ> point_cloud;
	pcl::fromROSMsg(clusters_array.clusters.at(0).cloud, point_cloud);
	sensor_msgs::PointCloud2 cloud_msg;
	pcl::toROSMsg(point_cloud, cloud_msg);
	cloud_msg.header.frame_id = "map";
	pub_cluster_cloud.publish(cloud_msg);

	if(m_TrackedClusters.size()>0)
	{
		jsk_recognition_msgs::BoundingBoxArray boxes_array;
		boxes_array.header.frame_id = "map";
		boxes_array.header.stamp  = ros::Time();
		jsk_recognition_msgs::BoundingBox box;
		box.header.frame_id = "map";
		box.header.stamp = ros::Time();
		box.pose.position.x = m_TrackedClusters.at(0).center.pos.x;
		box.pose.position.y = m_TrackedClusters.at(0).center.pos.y;
		box.pose.position.z = m_TrackedClusters.at(0).center.pos.z;

		box.value = 0.9;

		//box.pose.orientation = detectedPolygons.markers.at(0)
		box.dimensions.x = SIMU_OBSTACLE_WIDTH;
		box.dimensions.y = SIMU_OBSTACLE_LENGTH;
		box.dimensions.z = SIMU_OBSTACLE_HEIGHT;
		boxes_array.boxes.push_back(box);

		pub_TrackedObstaclesRviz.publish(boxes_array);
	}

//	if(m_LocalPlanner.m_TotalPath.size() > 0)
//	{
//		vector<PlannerHNS::WayPoint> line;
//		PlannerHNS::WayPoint p1(msg->point.x+m_OriginPos.position.x, msg->point.y+m_OriginPos.position.y, msg->point.z+m_OriginPos.position.z, 0);
//
//		//int index = PlannerHNS::PlanningHelpers::GetClosestNextPointIndex(m_LocalPlanner.m_TotalPath.at(0), p1);
////		PlannerHNS::WayPoint p_prev = m_LocalPlanner.m_TotalPath.at(0).at(index);
////		if(index > 0)
////			p_prev = m_LocalPlanner.m_TotalPath.at(0).at(index-1);
////
////
////		double distance = 0;
////		PlannerHNS::WayPoint p2 = PlannerHNS::PlanningHelpers::GetPerpendicularOnTrajectory(m_LocalPlanner.m_TotalPath.at(0), p1, distance);
////
////		double perpDistance = PlannerHNS::PlanningHelpers::GetPerpDistanceToTrajectorySimple(m_LocalPlanner.m_TotalPath.at(0), p1);
////
////		double back_distance = hypot(p2.pos.y - p_prev.pos.y, p2.pos.x - p_prev.pos.x);
////		double direct_distance = hypot(p2.pos.y - p1.pos.y, p2.pos.x - p1.pos.x);
//
//
//		PlannerHNS::RelativeInfo info;
//		bool ret = PlannerHNS::PlanningHelpers::GetRelativeInfo(m_LocalPlanner.m_TotalPath.at(0), p1, info);
//		PlannerHNS::WayPoint p_prev = m_LocalPlanner.m_TotalPath.at(0).at(info.iBack);
//
//		std::cout << "Perp D: " << info.perp_distance << ", F D: "<< info.to_front_distance << ", B D: " << info.from_back_distance << ", F Index: "<< info.iFront << ", B Index: " << info.iBack << ", Size: "<< m_LocalPlanner.m_TotalPath.at(0).size() << std::endl;
//
//		line.push_back(p1);
//		line.push_back(info.perp_point);
//		line.push_back(p_prev);
//
//		std::vector<std::vector<PlannerHNS::WayPoint> > lines;
//		lines.push_back(line);
//		visualization_msgs::MarkerArray line_vis;
//		RosHelpers::ConvertFromPlannerHToAutowareVisualizePathFormat(lines, line_vis);
//		pub_TestLineRviz.publish(line_vis);
//	}
}

void PlannerX::callbackGetCurrentPose(const geometry_msgs::PoseStampedConstPtr& msg)
{
	m_counter++;
	double dt = UtilityHNS::UtilityH::GetTimeDiffNow(m_Timer);
	if(dt >= 1.0)
	{
		m_frequency = m_counter;
		m_counter = 0;
		clock_gettime(0, &m_Timer);
	}

	m_CurrentPos = PlannerHNS::WayPoint(msg->pose.position.x, msg->pose.position.y,
					msg->pose.position.z, tf::getYaw(msg->pose.orientation));

	m_InitPos = m_CurrentPos;

	bNewCurrentPos = true;
	bInitPos = true;
#ifdef DATASET_GENERATION_BLOCK

	PlannerHNS::WayPoint p(m_CurrentPos.pos.x, m_CurrentPos.pos.y, 0, m_CurrentPos.pos.a);
	p.v = m_VehicleState.speed;

	DataPairs dp;
	if(m_DrivePoints.size() == 0)
	{
		dp.image = m_CurrImage;
		dp.currentPos = p;
		dp.vehicleState = m_VehicleState;
		m_DrivePoints.push_back(dp);
	}
	else
	{
		p.cost = hypot(p.pos.y - m_DrivePoints.at(m_DrivePoints.size()-1).currentPos.pos.y, p.pos.x - m_DrivePoints.at(m_DrivePoints.size()-1).currentPos.pos.x);
		if(p.cost >= 0.25 && p.cost <= 3.0)
		{
			dp.image = m_CurrImage;
			dp.currentPos = p;
			dp.vehicleState = m_VehicleState;
			m_DrivePoints.push_back(dp);
			//std::cout << "Insert Pose: " << "Cost: " << p.cost << ", Speed: " << p.v << ", Size: " << m_DrivePoints.size() << std::endl;
		}
		else
		{
			//std::cout << "Miss Pose: " << "Cost: " << p.cost << ", Speed: " << p.v << ", Size: " << m_DrivePoints.size() << std::endl;
			if(m_DrivePoints.size() == 1)
				m_DrivePoints.clear();
		}
	}


	if(m_DrivePoints.size() >= 20)
		ExtractPathFromDriveData();

#endif
}

autoware_msgs::CloudCluster PlannerX::GenerateSimulatedObstacleCluster(const double& x_rand, const double& y_rand, const double& z_rand, const int& nPoints, const geometry_msgs::PointStamped& centerPose)
{
	autoware_msgs::CloudCluster cluster;
	cluster.centroid_point.point = centerPose.point;
	cluster.dimensions.x = x_rand;
	cluster.dimensions.y = y_rand;
	cluster.dimensions.z = z_rand;
	pcl::PointCloud<pcl::PointXYZ> point_cloud;

	timespec t;
	for(int i=1; i < nPoints; i++)
	{
		UtilityHNS::UtilityH::GetTickCount(t);
		pcl::PointXYZ p;
		srand(t.tv_nsec/i);
		double x = (double)(rand()%100)/100.0 - 0.5;

		srand(t.tv_nsec/i*i);
		double y = (double)(rand()%100)/100.0 - 0.5;

		srand(t.tv_nsec);
		double z = (double)(rand()%100)/100.0 - 0.5;

		p.x = centerPose.point.x + x*x_rand;
		p.y = centerPose.point.y + y*y_rand;
		p.z = centerPose.point.z + z*z_rand;
		point_cloud.points.push_back(p);
	}

	pcl::toROSMsg(point_cloud, cluster.cloud);

	return cluster;
}

void PlannerX::callbackGetCloudClusters(const autoware_msgs::CloudClusterArrayConstPtr& msg)
{
	timespec timerTemp;
	UtilityHNS::UtilityH::GetTickCount(timerTemp);

	m_OriginalClusters.clear();
	RosHelpers::ConvertFromAutowareCloudClusterObstaclesToPlannerH(m_CurrentPos, m_LocalPlanner.m_CarInfo, *msg, m_OriginalClusters, m_nOriginalPoints, m_nContourPoints);
	if(m_bEnableTracking)
	{
		m_ObstacleTracking.DoOneStep(m_CurrentPos, m_OriginalClusters);
		m_TrackedClusters = m_ObstacleTracking.m_DetectedObjects;
	}
	else
		m_TrackedClusters = m_OriginalClusters;

	m_nTrackObjects = m_TrackedClusters.size();
	m_TrackingTime = UtilityHNS::UtilityH::GetTimeDiffNow(timerTemp);
	bNewClusters = true;
}

void PlannerX::callbackGetBoundingBoxes(const jsk_recognition_msgs::BoundingBoxArrayConstPtr& msg)
{
//	std::cout << " Number of Detected Boxes =" << msg->boxes.size() << std::endl;
//	RosHelpers::ConvertFromAutowareBoundingBoxObstaclesToPlannerH(*msg, m_DetectedBoxes);
//	bNewBoxes = true;
}

void PlannerX::callbackGetVehicleStatus(const geometry_msgs::TwistStampedConstPtr& msg)
{
	m_VehicleState.speed = msg->twist.linear.x;

	if(msg->twist.linear.x != 0)
		m_VehicleState.steer = atan(m_LocalPlanner.m_CarInfo.wheel_base * msg->twist.angular.z/msg->twist.linear.x);

	UtilityHNS::UtilityH::GetTickCount(m_VehicleState.tStamp);

	// If steering is in angular velocity
	//m_VehicleState.steer = atan(m_State.m_CarInfo.wheel_base * msg->twist.angular.z/msg->twist.linear.x);
//	if(msg->vector.z == 0x00)
//		m_VehicleState.shift = AW_SHIFT_POS_BB;
//	else if(msg->vector.z == 0x10)
//		m_VehicleState.shift = AW_SHIFT_POS_DD;
//	else if(msg->vector.z == 0x20)
//		m_VehicleState.shift = AW_SHIFT_POS_NN;
//	else if(msg->vector.z == 0x40)
//		m_VehicleState.shift = AW_SHIFT_POS_RR;

	//std::cout << "PlannerX: Read Status Twist_cmd ("<< m_VehicleState.speed << ", " << m_VehicleState.steer<<")" << std::endl;
}

void PlannerX::callbackGetCanInfo(const autoware_msgs::CanInfoConstPtr &msg)
{
	m_VehicleState.speed = msg->speed/3.6;
	m_VehicleState.steer = msg->angle * m_LocalPlanner.m_CarInfo.max_steer_angle / m_LocalPlanner.m_CarInfo.max_steer_value;
	std::cout << "Can Info, Speed: "<< m_VehicleState.speed << ", Steering: " << m_VehicleState.steer  << std::endl;
}

void PlannerX::callbackGetRobotOdom(const nav_msgs::OdometryConstPtr& msg)
{
	m_VehicleState.speed = msg->twist.twist.linear.x;
	m_VehicleState.steer += atan(m_LocalPlanner.m_CarInfo.wheel_base * msg->twist.twist.angular.z/msg->twist.twist.linear.x);

	UtilityHNS::UtilityH::GetTickCount(m_VehicleState.tStamp);
//	if(msg->vector.z == 0x00)
//		m_VehicleState.shift = AW_SHIFT_POS_BB;
//	else if(msg->vector.z == 0x10)
//		m_VehicleState.shift = AW_SHIFT_POS_DD;
//	else if(msg->vector.z == 0x20)
//		m_VehicleState.shift = AW_SHIFT_POS_NN;
//	else if(msg->vector.z == 0x40)
//		m_VehicleState.shift = AW_SHIFT_POS_RR;

	//std::cout << "PlannerX: Read Odometry ("<< m_VehicleState.speed << ", " << m_VehicleState.steer<<")" << std::endl;
}

void PlannerX::callbackGetEmergencyStop(const std_msgs::Int8& msg)
{
	//std::cout << "Received Emergency Stop : " << msg.data << std::endl;
	bNewEmergency  = true;
	m_bEmergencyStop = msg.data;
}

void PlannerX::callbackGetTrafficLight(const std_msgs::Int8& msg)
{
	std::cout << "Received Traffic Light : " << msg.data << std::endl;
	bNewTrafficLigh = true;
	if(msg.data == 2)
		m_bGreenLight = true;
	else
		m_bGreenLight = false;
}

void PlannerX::callbackGetOutsideControl(const std_msgs::Int8& msg)
{
	std::cout << "Received Outside Control : " << msg.data << std::endl;
	bNewOutsideControl = true;
	m_bOutsideControl  = msg.data;
}

void PlannerX::callbackGetAStarPath(const autoware_msgs::LaneArrayConstPtr& msg)
{
	if(msg->lanes.size() > 0)
	{
		m_AStarPath.clear();
		for(unsigned int i = 0 ; i < msg->lanes.size(); i++)
		{
			for(unsigned int j = 0 ; j < msg->lanes.at(i).waypoints.size(); j++)
			{
				PlannerHNS::WayPoint wp(msg->lanes.at(i).waypoints.at(j).pose.pose.position.x,
						msg->lanes.at(i).waypoints.at(j).pose.pose.position.y,
						msg->lanes.at(i).waypoints.at(j).pose.pose.position.z,
						tf::getYaw(msg->lanes.at(i).waypoints.at(j).pose.pose.orientation));
				wp.v = msg->lanes.at(i).waypoints.at(j).twist.twist.linear.x;
				//wp.bDir = msg->lanes.at(i).waypoints.at(j).dtlane.dir;
				m_AStarPath.push_back(wp);
			}
		}
		bNewAStarPath = true;
		m_LocalPlanner.m_pCurrentBehaviorState->GetCalcParams()->bRePlan = true;
	}
}

void PlannerX::callbackGetWayPlannerPath(const autoware_msgs::LaneArrayConstPtr& msg)
{
	if(msg->lanes.size() > 0)
	{
		m_WayPlannerPaths.clear();
		bool bOldGlobalPath = m_LocalPlanner.m_TotalPath.size() == msg->lanes.size();
		for(unsigned int i = 0 ; i < msg->lanes.size(); i++)
		{
			std::vector<PlannerHNS::WayPoint> path;
			PlannerHNS::Lane* pPrevValid = 0;
			for(unsigned int j = 0 ; j < msg->lanes.at(i).waypoints.size(); j++)
			{
				PlannerHNS::WayPoint wp(msg->lanes.at(i).waypoints.at(j).pose.pose.position.x,
						msg->lanes.at(i).waypoints.at(j).pose.pose.position.y,
						msg->lanes.at(i).waypoints.at(j).pose.pose.position.z,
						tf::getYaw(msg->lanes.at(i).waypoints.at(j).pose.pose.orientation));
				wp.v = msg->lanes.at(i).waypoints.at(j).twist.twist.linear.x;
				wp.laneId = msg->lanes.at(i).waypoints.at(j).twist.twist.linear.y;
				wp.stopLineID = msg->lanes.at(i).waypoints.at(j).twist.twist.linear.z;
				wp.laneChangeCost = msg->lanes.at(i).waypoints.at(j).twist.twist.angular.x;
				wp.LeftLaneId = msg->lanes.at(i).waypoints.at(j).twist.twist.angular.y;
				wp.RightLaneId = msg->lanes.at(i).waypoints.at(j).twist.twist.angular.z;

				if(msg->lanes.at(i).waypoints.at(j).dtlane.dir == 0)
					wp.bDir = PlannerHNS::FORWARD_DIR;
				else if(msg->lanes.at(i).waypoints.at(j).dtlane.dir == 1)
					wp.bDir = PlannerHNS::FORWARD_LEFT_DIR;
				else if(msg->lanes.at(i).waypoints.at(j).dtlane.dir == 2)
					wp.bDir = PlannerHNS::FORWARD_RIGHT_DIR;

				PlannerHNS::Lane* pLane = 0;
				pLane = PlannerHNS::MappingHelpers::GetLaneById(wp.laneId, m_Map);
				if(!pLane)
				{
					pLane = PlannerHNS::MappingHelpers::GetClosestLaneFromMapDirectionBased(wp, m_Map, 1);

					if(!pLane && !pPrevValid)
					{
						ROS_ERROR("Map inconsistency between Global Path add Lal Planer, Can't identify current lane.");
						return;
					}

					if(!pLane)
						wp.pLane = pPrevValid;
					else
					{
						wp.pLane = pLane;
						pPrevValid = pLane ;
					}

					wp.laneId = wp.pLane->id;
				}
				else
					wp.pLane = pLane;

				path.push_back(wp);
			}

			PlannerHNS::PlanningHelpers::CalcAngleAndCost(path);

//			int prevStopID = -1;
//			for(unsigned int k= 0; k < path.size(); k++)
//			{
//				if(path.at(k).pLane)
//				{
//					for(unsigned int si = 0; si < path.at(k).pLane->stopLines.size(); si++)
//					{
//						if(prevStopID != path.at(k).pLane->stopLines.at(si).id)
//						{
//							PlannerHNS::WayPoint stopLineWP;
//							stopLineWP.pos = path.at(k).pLane->stopLines.at(si).points.at(0);
//							PlannerHNS::RelativeInfo info;
//							PlannerHNS::PlanningHelpers::GetRelativeInfo(path, stopLineWP, info, k);
//
//							path.at(info.iFront).stopLineID = path.at(k).pLane->stopLines.at(si).id;
//							prevStopID = path.at(info.iFront).stopLineID;
//						}
//					}
//				}
//			}

			m_WayPlannerPaths.push_back(path);

			if(bOldGlobalPath)
			{
				bOldGlobalPath = PlannerHNS::PlanningHelpers::CompareTrajectories(path, m_LocalPlanner.m_TotalPath.at(i));
			}
		}


		if(!bOldGlobalPath)
		{
			bWayPlannerPath = true;
			m_LocalPlanner.m_pCurrentBehaviorState->GetCalcParams()->bNewGlobalPath = true;
			//m_CurrentGoal = m_WayPlannerPaths.at(0).at(m_WayPlannerPaths.at(0).size()-1);
			m_LocalPlanner.m_TotalPath = m_WayPlannerPaths;

			cout << "Global Lanes Size = " << msg->lanes.size() <<", Conv Size= " << m_WayPlannerPaths.size() << ", First Lane Size: " << m_WayPlannerPaths.at(0).size() << endl;

//			for(unsigned int k= 0; k < m_WayPlannerPaths.at(0).size(); k++)
//			{
//				if(m_WayPlannerPaths.at(0).at(k).stopLineID > 0 && m_WayPlannerPaths.at(0).at(k).pLane && m_WayPlannerPaths.at(0).at(k).pLane->stopLines.size()>0)
//				{
//					cout << "Stop Line IDs: " << m_WayPlannerPaths.at(0).at(k).stopLineID << ", Lane: " << m_WayPlannerPaths.at(0).at(k).pLane << ", Stop Lines: "<< m_WayPlannerPaths.at(0).at(k).pLane->stopLines.size() << endl;
//				}
//			}
		}
	}
}

void PlannerX::PlannerMainLoop()
{
	ros::Rate loop_rate(100);

	timespec trackingTimer;
	UtilityHNS::UtilityH::GetTickCount(trackingTimer);
	PlannerHNS::WayPoint prevState, state_change;

	while (ros::ok())
	{
		timespec iterationTime;
		UtilityHNS::UtilityH::GetTickCount(iterationTime);

		ros::spinOnce();

		if(m_MapSource == MAP_KML_FILE && !bKmlMapLoaded)
		{
			bKmlMapLoaded = true;
			PlannerHNS::MappingHelpers::LoadKML(m_KmlMapPath, m_Map);
			//sub_WayPlannerPaths = nh.subscribe("/lane_waypoints_array", 	10,		&PlannerX::callbackGetWayPlannerPath, 	this);
		}
		else if(m_MapSource == MAP_FOLDER && !bKmlMapLoaded)
		{
			bKmlMapLoaded = true;
			PlannerHNS::MappingHelpers::ConstructRoadNetworkFromDataFiles(m_KmlMapPath, m_Map, true);
		}
		else if(m_MapSource == MAP_AUTOWARE)
		{
			 if(m_AwMap.bDtLanes && m_AwMap.bLanes && m_AwMap.bPoints)
			 {
				timespec timerTemp;
				UtilityHNS::UtilityH::GetTickCount(timerTemp);
				 m_AwMap.bDtLanes = m_AwMap.bLanes = m_AwMap.bPoints = false;
				 RosHelpers::UpdateRoadMap(m_AwMap,m_Map);
				 std::cout << "Converting Vector Map Time : " <<UtilityHNS::UtilityH::GetTimeDiffNow(timerTemp) << std::endl;
				 //sub_WayPlannerPaths = nh.subscribe("/lane_waypoints_array", 	10,		&PlannerX::callbackGetWayPlannerPath, 	this);
			 }
		}

		int iDirection = 0;
		if(bInitPos && m_LocalPlanner.m_TotalPath.size()>0)
		{
//			bool bMakeNewPlan = false;
//			double drift = hypot(m_LocalPlanner.state.pos.y-m_CurrentPos.pos.y, m_LocalPlanner.state .pos.x-m_CurrentPos.pos.x);
//			if(drift > 10)
//				bMakeNewPlan = true;

			m_LocalPlanner.m_pCurrentBehaviorState->GetCalcParams()->bOutsideControl = m_bOutsideControl;
			m_LocalPlanner.state = m_CurrentPos;

			double dt  = UtilityHNS::UtilityH::GetTimeDiffNow(m_PlanningTimer);
			UtilityHNS::UtilityH::GetTickCount(m_PlanningTimer);

			m_CurrentBehavior = m_LocalPlanner.DoOneStep(dt, m_VehicleState, m_TrackedClusters, 1, m_Map, m_bEmergencyStop, m_bGreenLight, true);

			visualization_msgs::Marker behavior_rviz;

			if(m_LocalPlanner.m_pCurrentBehaviorState->GetCalcParams()->iCurrSafeTrajectory > m_LocalPlanner.m_pCurrentBehaviorState->GetCalcParams()->iCentralTrajectory)
				iDirection = 1;
			else if(m_LocalPlanner.m_pCurrentBehaviorState->GetCalcParams()->iCurrSafeTrajectory < m_LocalPlanner.m_pCurrentBehaviorState->GetCalcParams()->iCentralTrajectory)
				iDirection = -1;

			RosHelpers::VisualizeBehaviorState(m_CurrentPos, m_CurrentBehavior, m_bGreenLight, iDirection, behavior_rviz);

			pub_BehaviorStateRviz.publish(behavior_rviz);

			if(m_CurrentBehavior.state != m_PrevBehavior.state)
			{
				//std::cout << m_LocalPlanner.m_pCurrentBehaviorState->GetCalcParams()->ToString(m_CurrentBehavior.state) << ", Speed : " << m_CurrentBehavior.maxVelocity << std::endl;
				m_PrevBehavior = m_CurrentBehavior;
			}


			geometry_msgs::Twist t;
			geometry_msgs::TwistStamped behavior;
			t.linear.x = m_CurrentBehavior.followDistance;
			t.linear.y = m_CurrentBehavior.stopDistance;
			t.linear.z = (int)m_CurrentBehavior.indicator;

			t.angular.x = m_CurrentBehavior.followVelocity;
			t.angular.y = m_CurrentBehavior.maxVelocity;
			t.angular.z = (int)m_CurrentBehavior.state;

			behavior.twist = t;
			behavior.header.stamp = ros::Time::now();

			pub_BehaviorState.publish(behavior);

			visualization_msgs::MarkerArray detectedPolygons;
			RosHelpers::ConvertFromPlannerObstaclesToAutoware(m_CurrentPos, m_TrackedClusters, detectedPolygons);
			pub_DetectedPolygonsRviz.publish(detectedPolygons);

			visualization_msgs::Marker safety_box;
			RosHelpers::ConvertFromPlannerHRectangleToAutowareRviz(m_LocalPlanner.m_TrajectoryCostsCalculatotor.m_SafetyBorder.points, safety_box);
			pub_SafetyBorderRviz.publish(safety_box);

			geometry_msgs::PoseArray sim_data;
			geometry_msgs::Pose p_id, p_pose, p_box;


			sim_data.header.frame_id = "map";
			sim_data.header.stamp = ros::Time();

			p_id.position.x = 0;

			p_pose.orientation = tf::createQuaternionMsgFromRollPitchYaw(0, 0, UtilityHNS::UtilityH::SplitPositiveAngle(m_LocalPlanner.state.pos.a));
			p_pose.position.x = m_LocalPlanner.state.pos.x;
			p_pose.position.y = m_LocalPlanner.state.pos.y;
			p_pose.position.z = m_LocalPlanner.state.pos.z;



			p_box.position.x = m_LocalPlanner.m_CarInfo.width;
			p_box.position.y = m_LocalPlanner.m_CarInfo.length;
			p_box.position.z = 2.2;

			sim_data.poses.push_back(p_id);
			sim_data.poses.push_back(p_pose);
			sim_data.poses.push_back(p_box);

			pub_SimuBoxPose.publish(sim_data);

			timespec log_t;
			UtilityHNS::UtilityH::GetTickCount(log_t);
			std::ostringstream dataLine;
			std::ostringstream dataLineToOut;
			dataLine << UtilityHNS::UtilityH::GetLongTime(log_t) <<"," << dt << "," << m_CurrentBehavior.state << ","<< RosHelpers::GetBehaviorNameFromCode(m_CurrentBehavior.state) << "," <<
					m_nTrackObjects << "," << m_nOriginalPoints << "," << m_nContourPoints << "," << m_TrackingTime << "," <<
					m_LocalPlanner.m_CostCalculationTime << "," << m_LocalPlanner.m_BehaviorGenTime << "," << m_LocalPlanner.m_RollOutsGenerationTime << "," <<
					m_LocalPlanner.m_pCurrentBehaviorState->m_pParams->rollOutNumber << "," <<
					m_LocalPlanner.m_pCurrentBehaviorState->GetCalcParams()->bFullyBlock << "," <<
					m_LocalPlanner.m_pCurrentBehaviorState->GetCalcParams()->iCentralTrajectory << "," <<
					m_LocalPlanner.m_iSafeTrajectory << "," <<
					m_LocalPlanner.m_pCurrentBehaviorState->GetCalcParams()->currentStopSignID << "," <<
					m_LocalPlanner.m_pCurrentBehaviorState->GetCalcParams()->currentTrafficLightID << "," <<
					m_LocalPlanner.m_pCurrentBehaviorState->GetCalcParams()->minStoppingDistance << "," <<
					m_LocalPlanner.m_pCurrentBehaviorState->GetCalcParams()->distanceToNext << "," <<
					m_LocalPlanner.m_pCurrentBehaviorState->GetCalcParams()->velocityOfNext << "," <<
					m_VehicleState.speed << "," <<
					m_VehicleState.steer << "," <<
					m_LocalPlanner.state.pos.x << "," << m_LocalPlanner.state.pos.y << "," << m_LocalPlanner.state.pos.z << "," << UtilityHNS::UtilityH::SplitPositiveAngle(m_LocalPlanner.state.pos.a)+M_PI << ",";
			m_LogData.push_back(dataLine.str());

//			dataLineToOut << RosHelpers::GetBehaviorNameFromCode(m_CurrentBehavior.state) << ","
//					<< m_LocalPlanner.m_pCurrentBehaviorState->GetCalcParams()->bFullyBlock << ","
//					<< m_LocalPlanner.m_iSafeTrajectory << ","
//					<< m_LocalPlanner.m_pCurrentBehaviorState->GetCalcParams()->minStoppingDistance << ","
//					<< m_LocalPlanner.m_pCurrentBehaviorState->GetCalcParams()->distanceToNext << ","
//					<< m_LocalPlanner.m_pCurrentBehaviorState->GetCalcParams()->velocityOfNext << ","
//					<< m_LocalPlanner.m_pCurrentBehaviorState->GetCalcParams()->currentStopSignID << ","
//					<< m_LocalPlanner.m_pCurrentBehaviorState->GetCalcParams()->currentTrafficLightID << ",";
//
//			cout << dataLineToOut.str() << endl;


		}
		else
		{
			UtilityHNS::UtilityH::GetTickCount(m_PlanningTimer);
			sub_WayPlannerPaths = nh.subscribe("/lane_waypoints_array", 	1,		&PlannerX::callbackGetWayPlannerPath, 	this);
		}


		autoware_msgs::lane current_trajectory;
		std_msgs::Int32 closest_waypoint;
		PlannerHNS::RelativeInfo info;
		PlannerHNS::PlanningHelpers::GetRelativeInfo(m_LocalPlanner.m_Path, m_LocalPlanner.state, info);
		RosHelpers::ConvertFromPlannerHToAutowarePathFormat(m_LocalPlanner.m_Path, info.iBack, current_trajectory);
		closest_waypoint.data = 1;
		pub_ClosestIndex.publish(closest_waypoint);
		pub_LocalBasePath.publish(current_trajectory);
		pub_LocalPath.publish(current_trajectory);
		visualization_msgs::MarkerArray all_rollOuts;

	
		RosHelpers::ConvertFromPlannerHToAutowareVisualizePathFormat(m_LocalPlanner.m_Path, m_LocalPlanner.m_RollOuts, m_LocalPlanner, all_rollOuts);
		pub_LocalTrajectoriesRviz.publish(all_rollOuts);

		//Publish markers that visualize only when avoiding objects
		if(enablePlannerDynamicSwitch){
			visualization_msgs::MarkerArray all_rollOuts_dynamic;
			std_msgs::Int32 enableLattice;
			if(iDirection != 0) { // if obstacle avoidance state,
				all_rollOuts_dynamic = all_rollOuts;
		   		
			     	for(auto &ro : all_rollOuts_dynamic.markers){
					ro.ns = "global_lane_array_marker_dynamic";
				}
				pub_LocalTrajectoriesRviz_dynamic.publish(all_rollOuts_dynamic);
				enableLattice.data = 1;
			}else{
				visualization_msgs::Marker delMarker;
				delMarker.action = visualization_msgs::Marker::DELETEALL;
				delMarker.ns = "global_lane_array_marker_dynamic";
				all_rollOuts_dynamic.markers.push_back(delMarker);
				pub_LocalTrajectoriesRviz_dynamic.publish(all_rollOuts_dynamic);
				enableLattice.data = 0;
			}
			pub_EnableLattice.publish(enableLattice); //Publish flag of object avoidance
		}


		if(m_CurrentBehavior.bNewPlan)
		{
			std::ostringstream str_out;
			str_out << UtilityHNS::UtilityH::GetHomeDirectory();
			str_out << UtilityHNS::DataRW::LoggingMainfolderName;
			str_out << UtilityHNS::DataRW::PathLogFolderName;
			str_out << "LocalPath_";
			PlannerHNS::PlanningHelpers::WritePathToFile(str_out.str(), m_LocalPlanner.m_Path);
		}


		//Traffic Light Simulation Part
		if(m_bGreenLight && UtilityHNS::UtilityH::GetTimeDiffNow(m_TrafficLightTimer) > 5)
		{
			m_bGreenLight = false;
			UtilityHNS::UtilityH::GetTickCount(m_TrafficLightTimer);
		}
		else if(!m_bGreenLight && UtilityHNS::UtilityH::GetTimeDiffNow(m_TrafficLightTimer) > 10.0)
		{
			m_bGreenLight = true;
			UtilityHNS::UtilityH::GetTickCount(m_TrafficLightTimer);
		}

		loop_rate.sleep();

		//double onePassTime = UtilityHNS::UtilityH::GetTimeDiffNow(iterationTime);
//		if(onePassTime > 0.1)
//			std::cout << "Slow Iteration Time = " << onePassTime << " , for Obstacles : (" << m_TrackedClusters.size() << ", " << m_OriginalClusters.size() << ")" <<  std::endl;
	}
}

}
