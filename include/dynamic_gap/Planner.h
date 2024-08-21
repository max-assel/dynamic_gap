#pragma once

#include <ros/ros.h>

// #include <boost/numeric/odeint.hpp>
#include <boost/shared_ptr.hpp>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <cmath>
#include <numeric>
#include <iostream>
#include <chrono>

#include <math.h>

#include <geometry_msgs/TransformStamped.h>
#include <geometry_msgs/PoseArray.h>
#include <sensor_msgs/LaserScan.h>
// #include <std_msgs/Header.h>
#include <nav_msgs/Odometry.h>

#include <dynamic_gap/utils/Gap.h>
#include <dynamic_gap/utils/Trajectory.h>
#include <dynamic_gap/utils/Utils.h>
#include <dynamic_gap/gap_estimation/GapAssociator.h>
#include <dynamic_gap/gap_detection/GapDetector.h>
#include <dynamic_gap/config/DynamicGapConfig.h>
#include <dynamic_gap/visualization/GapVisualizer.h>
#include <dynamic_gap/visualization/GoalVisualizer.h>
#include <dynamic_gap/visualization/TrajectoryVisualizer.h>
#include <dynamic_gap/global_plan_management/GlobalPlanManager.h>
#include <dynamic_gap/trajectory_scoring/TrajectoryScorer.h>
#include <dynamic_gap/trajectory_generation/GapManipulator.h>
#include <dynamic_gap/trajectory_generation/NavigableGapGenerator.h>
#include <dynamic_gap/trajectory_tracking/TrajectoryController.h>
#include <dynamic_gap/gap_feasibility/GapFeasibilityChecker.h>

#include <tf2_ros/transform_listener.h>
#include <tf2_ros/transform_broadcaster.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>

#include <boost/thread/mutex.hpp>
#include <boost/circular_buffer.hpp>

namespace dynamic_gap
{
    /**
    * \brief Class responsible for the core planning loop of dynamic gap.
    */
    class Planner
    {

        public:
            Planner();

            ~Planner();

            /**
            * \brief initialize Planner class
            * 
            * \param nh ROS node handle 
            * \return initialization success / failure
            */
            bool initialize(const ros::NodeHandle& nh);

            /**
            * \brief Check if global goal has been reached by robot
            * \return boolean for if global goal has been reached or not
            */
            bool isGoalReached();

            /**
            * \brief Call back function to robot laser scan
            * \param scan incoming laser scan msg
            */
            void laserScanCB(boost::shared_ptr<sensor_msgs::LaserScan const> scan);

            /**
            * \brief Function for updating the gap models
            * \param gaps set of gaps whose models we are updating
            * \param intermediateRbtVels intermediate robot velocity values between last model update and current model update
            * \param intermediateRbtAccs intermediate robot acceleration values between last model update and current model update
            * \param tCurrentFilterUpdate time step for current estimator update
            */
            void updateModels(std::vector<dynamic_gap::Gap *> & gaps, 
                                const std::vector<geometry_msgs::TwistStamped> & intermediateRbtVels,
                                const std::vector<geometry_msgs::TwistStamped> & intermediateRbtAccs,
                                const ros::Time & tCurrentFilterUpdate);

            /**
            * \brief Function for updating a single gap's models
            * \param idx index of gap whose models we must update
            * \param gaps set of gaps whose models we are updating
            * \param intermediateRbtVels intermediate robot velocity values between last model update and current model update
            * \param intermediateRbtAccs intermediate robot acceleration values between last model update and current model update
            * \param tCurrentFilterUpdate time step for current estimator update
            */
            void updateModel(const int & idx, 
                                std::vector<dynamic_gap::Gap *> & gaps, 
                                const std::vector<geometry_msgs::TwistStamped> & intermediateRbtVels,
                                const std::vector<geometry_msgs::TwistStamped> & intermediateRbtAccs,
                                const ros::Time & tCurrentFilterUpdate);

            /**
            * \brief Joint call back function for robot pose (position + velocity) and robot acceleration messages
            * \param rbtOdomMsg incoming robot odometry message
            * \param rbtAccelMsg incoming robot acceleration message
            */
            void jointPoseAccCB(const nav_msgs::Odometry::ConstPtr & rbtOdomMsg, 
                                const geometry_msgs::TwistStamped::ConstPtr & rbtAccelMsg);

            /**
            * \brief Call back function for other agent odometry messages
            * \param agentOdomMsg incoming agent odometry message
            */
            void agentOdomCB(const nav_msgs::Odometry::ConstPtr& agentOdomMsg);

            /**
            * \brief Interface function for receiving incoming global plan and updating
            * the global plan local waypoint
            * \param globalPlanMapFrame incoming global plan in map frame
            * \return boolean type on whether planner successfully registered goal
            */
            bool setGoal(const std::vector<geometry_msgs::PoseStamped> & globalPlanMapFrame);

            /**
            * \brief Function for updating all tf transform at the beginning of every planning cycle
            */
            void updateTF();

            /**
            * \brief Function for all member objects updating their current egocircles
            */
            void updateEgoCircle();

            /**
            * \brief Function for evaluating the feasibility of a set of gaps
            * \param isCurrentGapFeasible boolean for if the gap the robot is currently traveling through is feasible
            * \return set of feasible gaps
            */
            std::vector<dynamic_gap::Gap *> gapSetFeasibilityCheck(bool & isCurrentGapFeasible);

            /**
            * \brief Function for performing gap manipulation steps
            * \return manipulated set of gaps
            */
            std::vector<dynamic_gap::Gap *> gapManipulate(const std::vector<dynamic_gap::Gap *> & feasibleGaps);

            /**
            * \brief Function for generating candidate trajectories through the current set of gaps
            * \param gaps incoming set of gaps through which we want to generate trajectories
            * \param generatedTrajs set of generated trajectories
            * \return Vector of pose-wise scores for the generated trajectories
            */
            std::vector<std::vector<float>> generateGapTrajs(std::vector<dynamic_gap::Gap *> & gaps, 
                                                             std::vector<dynamic_gap::Trajectory> & generatedTrajs,
                                                             const std::vector<sensor_msgs::LaserScan> & futureScans);

            /**
            * \brief Function for selecting the best trajectory out of the set of recently generated trajectories
            * \param trajs set of recently generated trajectories
            * \param pathPoseScores vector of pose-wise scores for the generated trajectories
            * \return index of the highest score trajectory
            */
            int pickTraj(const std::vector<dynamic_gap::Trajectory> & trajs, 
                        const std::vector<std::vector<float>> & pathPoseScores);

            /**
            * \brief Helper function for switching to a new trajectory
            * \param incomingGap incoming gap to switch to
            * \param incomingTraj incoming trajectory to switch to
            * \param switchToIncoming boolean for if planner is to switch to the incoming trajectory
            * \return trajectory that planner will switch to
            */
            dynamic_gap::Trajectory changeTrajectoryHelper(dynamic_gap::Gap * incomingGap, 
                                                            const dynamic_gap::Trajectory & incomingTraj, 
                                                            const bool & switchToIncoming);

            /**
            * \brief Compare incoming highest scoring trajectory to the trajectory that the
            * robot is currently following to determine if robot should switch to the incoming trajectory
            * \param incomingGap incoming gap to switch to
            * \param incomingTraj incoming trajectory to switch to
            * \param feasibleGaps set of feasible gaps which we are manipulating
            * \param isIncomingGapFeasible boolean for if the incoming gap is feasible 
            * \return the trajectory that the robot will track
            */
            dynamic_gap::Trajectory compareToCurrentTraj(dynamic_gap::Gap * incomingGap, 
                                                            const dynamic_gap::Trajectory & incomingTraj,                                                        
                                                            const std::vector<dynamic_gap::Gap *> & feasibleGaps, 
                                                            const bool & isIncomingGapFeasible,
                                                            const std::vector<sensor_msgs::LaserScan> & futureScans);

            /**
            * \brief Function for getting index of closest pose in trajectory
            * \param currTrajRbtFrame current trajectory in robot frame
            * \return index of closest pose in trajectory
            */
            int getClosestTrajectoryPoseIdx(const geometry_msgs::PoseArray & currTrajRbtFrame);

            /**
            * \brief Function for core planning loop of dynamic gap
            * \return selected trajectory in odometry frame to track
            */
            dynamic_gap::Trajectory runPlanningLoop();        

            /**
            * \brief Generate command velocity based on trajectory tracking and safety modules
            * \param localTrajectory selected local trajectory in odometry frame to track
            * \return command velocity to send to robot
            */
            geometry_msgs::Twist ctrlGeneration(const geometry_msgs::PoseArray & localTrajectory);

            /**
            * \brief Final command velocity pre-processing to check if robot is stuck
            * and planning has failed
            * \param cmdVel most recent command velocity
            * \return boolean for if planner is functioning and command velocity is fine
            */
            bool recordAndCheckVel(const geometry_msgs::Twist & cmdVel);

            /**
            * \brief Apply hard reset to planner to empty command velocity buffer
            * and wipe currently tracking trajectory
            */
            void reset();

            /**
            * \brief Setter for current gap
            * \param currentGap incoming gap that robot will be attempting to pass through
            */
            void setCurrentGap(dynamic_gap::Gap * currentGap) { delete currentGap_; currentGap_ = currentGap; return; }

            /**
            * \brief Getter for current gap
            * \return gap that robot is currently attempting to pass through            
            */
            dynamic_gap::Gap * getCurrentGap() { return currentGap_; }

            /**
            * \brief Setter for current trajectory
            * \param currentTraj incoming trajectory that robot is going to start tracking
            */
            void setCurrentTraj(const dynamic_gap::Trajectory & currentTraj) { currentTraj_ = currentTraj; return; }

            /**
            * \brief Getter for current trajectory
            * \return trajectory that robot is currently tracking
            */
            dynamic_gap::Trajectory getCurrentTraj() { return currentTraj_; }

            /**
            * \brief Getter for number of agents currently in environment
            * \return number of agents currently in environment
            */
            int getCurrentAgentCount() { return currentAgentCount_; }

            /**
            * \brief Indicator for if planner has been initialized
            * \return boolean for if planner has been initialized 
            */
            int initialized() { return initialized_; } 

            /**
            * \brief Setter for estimator of current gap's left point
            * \param leftModel estimator of current gap's left point
            */
            void setCurrentLeftModel(dynamic_gap::Estimator * leftModel);

            /**
            * \brief Setter for estimator of current gap's left point
            * \param rightModel estimator of current gap's right point
            */            
            void setCurrentRightModel(dynamic_gap::Estimator * rightModel);

            /**
            * \brief Getter for ID of model for current gap's left point
            * \return ID of model for current gap's left point
            */
            int getCurrentLeftGapPtModelID();

            /**
            * \brief Getter for ID of model for current gap's left point
            * \return ID of model for current gap's right point
            */            
            int getCurrentRightGapPtModelID();

            /**
            * \brief Setter for peak velocity of approximated 
            * spline trajectory through current gap
            * \param peakVelX peak x-velocity of approximated spline trajectory through current gap
            * \param peakVelY peak y-velocity of approximated spline trajectory through current gap            
            */
            void setCurrentGapPeakVelocities(const float & peakVelX, 
                                             const float & peakVelY);

            /**
            * \brief Helper function for pretty printing estimator states 
            * the left and right points of the current set of gaps
            * \param gaps current set of gaps
            */
            void printGapModels(const std::vector<dynamic_gap::Gap *> & gaps);

            /**
            * \brief Helper function for visualizing navigable regions of current set of gaps
            * \param manipulatedGaps current set of manipulated gaps
            */
            void visualizeNavigableGaps(const std::vector<dynamic_gap::Gap *> & manipulatedGaps,
                                        const int & highestScoreTrajIdx);    

    private:
        boost::mutex gapMutex_; /**< Current set of gaps mutex */
        dynamic_gap::DynamicGapConfig cfg_; /**< Planner hyperparameter config list */

        ros::NodeHandle nh_; /**< ROS node handle for local path planner */
        ros::Publisher currentTrajectoryPublisher_; /**< ROS publisher for currently tracked trajectory */
        // ros::Publisher staticScanPublisher_;

        geometry_msgs::TransformStamped map2rbt_; /**< Transformation from map frame to robot frame */
        geometry_msgs::TransformStamped odom2rbt_; /**< Transformation from odometry frame to robot frame */
        geometry_msgs::TransformStamped rbt2odom_; /**< Transformation from robot frame to odometry frame */
        geometry_msgs::TransformStamped map2odom_; /**< Transformation from map frame to odometry frame */
        geometry_msgs::TransformStamped cam2odom_; /**< Transformation from camera frame to odometry frame */
        geometry_msgs::TransformStamped rbt2cam_; /**< Transformation from robot frame to camera frame */

        geometry_msgs::PoseStamped rbtPoseInRbtFrame_; /**< Robot pose in robot frame */
        geometry_msgs::PoseStamped rbtPoseInSensorFrame_; /**< Robot pose in sensor frame */
        geometry_msgs::PoseStamped rbtPoseInOdomFrame_; /**< Robot pose in odometry frame */

        tf2_ros::Buffer tfBuffer_; /**< ROS transform buffer */
        tf2_ros::TransformListener * tfListener_ = NULL; /**< ROS transform listener */

        std::vector<int> rawAssocation_; /**< Association vector for current set of raw gaps */
        std::vector<int> simpAssociation_; /**< Association vector for current set of simplified gaps */
        std::vector<std::vector<float>> rawDistMatrix_; /**< Distance matrix for current set of raw gaps */
        std::vector<std::vector<float>> simpDistMatrix_; /**< Distance matrix for current set of raw gaps */

        // Goals and stuff
        geometry_msgs::PoseStamped globalGoalOdomFrame_; /**< Global goal in odometry frame */
        geometry_msgs::PoseStamped globalGoalRobotFrame_; /**< Global goal in robot frame */
        geometry_msgs::PoseStamped globalPathLocalWaypointOdomFrame_; /**< Global path local waypoint in odometry frame */

        // Gaps
        std::vector<dynamic_gap::Gap *> currRawGaps_; /**< Current set of raw gaps */
        std::vector<dynamic_gap::Gap *> currSimplifiedGaps_; /**< Current set of simplified gaps */
        std::vector<dynamic_gap::Gap *> prevRawGaps_; /**< Previous set of raw gaps */
        std::vector<dynamic_gap::Gap *> prevSimplifiedGaps_; /**< Previous set of simplified gaps */

        dynamic_gap::Gap * currentGap_ = NULL; /**< Gap that robot is currently attempting to pass through */

        dynamic_gap::Estimator * currLeftGapPtModel_ = NULL; /**< Estimator of current gap's left point */
        dynamic_gap::Estimator * currRightGapPtModel_ = NULL; /**< Estimator of current gap's right point */

        int currentModelIdx_ = 0; /**< Counter for instantiated models throughout planner's existence */

        ros::Time tPreviousModelUpdate_; /**< ROS time step of previous gap point model update */

        // Trajectories
        dynamic_gap::Trajectory currentTraj_; /**< Trajectory that robot is currently tracking */

        geometry_msgs::TwistStamped currentPeakSplineVel_; /**< Peak velocity along approximated spline trajectory through current gap */

        int targetTrajectoryPoseIdx_ = 0; /**< Index of closest pose along the robot's current trajectory */

        int trajectoryChangeCount_ = 0; /**< Counter for times that planner has switched local trajectories */

        // Scans
        // sensor_msgs::LaserScan staticScan_;
        boost::shared_ptr<sensor_msgs::LaserScan const> scan_; /**< Current laser scan */

        // Agents

        int currentAgentCount_; /**< Number of agents in environment */

        std::vector<geometry_msgs::Pose> currentTrueAgentPoses_; /**< Ground truth poses of agents currently in local environment */
        std::vector<geometry_msgs::Vector3Stamped> currentTrueAgentVels_; /**< Ground truth velocities of agents currently in local environment */

        std::vector<Eigen::Vector4f> currentEstimatedAgentStates_; /**< Estimated states of agents currently in local environment */

        dynamic_gap::GapDetector * gapDetector_ = NULL; /**< Gap detector */
        dynamic_gap::GapVisualizer * gapVisualizer_ = NULL; /**< Gap visualizer */
        dynamic_gap::GlobalPlanManager * globalPlanManager_ = NULL; /**< Goal selector */
        dynamic_gap::TrajectoryVisualizer * trajVisualizer_ = NULL; /**< Trajectory visualizer */
        dynamic_gap::GoalVisualizer * goalVisualizer_ = NULL; /**< Goal visualizer */
        dynamic_gap::TrajectoryScorer * trajScorer_ = NULL; /**< Trajectory scorer */
        dynamic_gap::GapTrajectoryGenerator * gapTrajGenerator_ = NULL; /**< Gap trajectory generator */
        dynamic_gap::GapManipulator * gapManipulator_ = NULL; /**< Gap manipulator */
        dynamic_gap::TrajectoryController * trajController_ = NULL; /**< Trajectory controller */
        dynamic_gap::GapAssociator * gapAssociator_ = NULL; /**< Gap associator */
        dynamic_gap::GapFeasibilityChecker * gapFeasibilityChecker_ = NULL; /**< Gap feasibility checker */
        dynamic_gap::NavigableGapGenerator * navigableGapGenerator_ = NULL; /**< Navigable gap generator */

        // Status
        bool hasGlobalGoal_ = false; /**< Indicator for if planner's global goal has been set */
        bool initialized_ = false; /**< Indicator for if planner has been initialized */
        bool readyToPlan = false; /**< Indicator for if planner has read in a laser scan and is ready to start planning */

        // Velocities
        boost::circular_buffer<float> cmdVelBuffer_; /**< Buffer of prior command velocities */

        geometry_msgs::TwistStamped currentRbtVel_; /**< Current robot velocity */
        geometry_msgs::TwistStamped currentRbtAcc_; /**< Current robot acceleration */

        std::vector<geometry_msgs::TwistStamped> intermediateRbtVels_; /**< Intermediate robot velocities between last model update and upcoming model update */
        std::vector<geometry_msgs::TwistStamped> intermediateRbtAccs_; /**< Intermediate robot accelerations between last model update and upcoming model update */
    };
}