#include <dynamic_gap/trajectory_scoring/TrajectoryScorer.h>


namespace dynamic_gap 
{
    TrajectoryScorer::TrajectoryScorer(ros::NodeHandle& nh, const dynamic_gap::DynamicGapConfig& cfg)
    {
        cfg_ = & cfg;
    }

    void TrajectoryScorer::updateEgoCircle(boost::shared_ptr<sensor_msgs::LaserScan const> scan) 
    {
        boost::mutex::scoped_lock lock(scanMutex_);
        scan_ = scan;
    }

    void TrajectoryScorer::transformGlobalPathLocalWaypointToRbtFrame(const geometry_msgs::PoseStamped & globalPathLocalWaypointOdomFrame, 
                                                                      const geometry_msgs::TransformStamped & odom2rbt) 
    {
        boost::mutex::scoped_lock lock(globalPlanMutex_);
        tf2::doTransform(globalPathLocalWaypointOdomFrame, globalPathLocalWaypointRobotFrame_, odom2rbt);
    }

    std::vector<float> TrajectoryScorer::scoreTrajectory(const dynamic_gap::Trajectory & traj,
                                                         const std::vector<sensor_msgs::LaserScan> & futureScans) 
    {    
        ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "         [scoreTrajectory()]");
        // Requires LOCAL FRAME
        // Should be no racing condition

        geometry_msgs::PoseArray path = traj.getPathRbtFrame();
        std::vector<float> pathTiming = traj.getPathTiming();
        
        float totalTrajCost = 0.0;
        std::vector<float> posewiseCosts;

        sensor_msgs::LaserScan dynamicLaserScan = sensor_msgs::LaserScan();

        float t_i = 0.0;
        float t_iplus1 = 0.0;

        std::vector<float> staticPosewiseCosts(path.poses.size());
        for (int i = 0; i < staticPosewiseCosts.size(); i++) 
        {
            // std::cout << "regular range at " << i << ": ";
            staticPosewiseCosts.at(i) = scorePose(path.poses.at(i)); //  / staticPosewiseCosts.size()
        }
        totalTrajCost = std::accumulate(staticPosewiseCosts.begin(), staticPosewiseCosts.end(), float(0));
        ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "             static pose-wise cost: " << totalTrajCost);
        // }


        posewiseCosts = staticPosewiseCosts;
        if (posewiseCosts.size() > 0) 
        {
            // obtain terminalGoalCost, scale by w1
            float terminalCost = cfg_->traj.Q_f * terminalGoalCost(*std::prev(path.poses.end()));
            // if the ending cost is less than 1 and the total cost is > -10, return trajectory of 100s
            if (terminalCost < 0.25 && totalTrajCost >= 0) 
            {
                // std::cout << "returning really good trajectory" << std::endl;
                return std::vector<float>(path.poses.size(), 100);
            }
            // Should be safe, subtract terminal pose cost from first pose cost
            ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "            terminal cost: " << terminalCost);
            posewiseCosts.at(0) += terminalCost;
        }
        
        // ROS_INFO_STREAM_NAMED("TrajectoryScorer", "scoreTrajectory time taken:" << ros::WallTime::now().toSec() - start_time);
        return posewiseCosts;
    }

    float TrajectoryScorer::terminalGoalCost(const geometry_msgs::Pose & pose) 
    {
        boost::mutex::scoped_lock planlock(globalPlanMutex_);
        // ROS_INFO_STREAM_NAMED("TrajectoryScorer", pose);
        ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "            final pose: (" << pose.position.x << ", " << pose.position.y << "), local goal: (" << globalPathLocalWaypointRobotFrame_.pose.position.x << ", " << globalPathLocalWaypointRobotFrame_.pose.position.y << ")");
        float dx = pose.position.x - globalPathLocalWaypointRobotFrame_.pose.position.x;
        float dy = pose.position.y - globalPathLocalWaypointRobotFrame_.pose.position.y;
        return sqrt(pow(dx, 2) + pow(dy, 2));
    }

    float TrajectoryScorer::scorePose(const geometry_msgs::Pose & pose) 
    {
        boost::mutex::scoped_lock lock(scanMutex_);
        sensor_msgs::LaserScan scan = *scan_.get();

        // obtain orientation and idx of pose

        // dist is size of scan
        std::vector<float> scan2RbtDists(scan.ranges.size());

        // iterate through ranges and obtain the distance from the egocircle point and the pose
        // Meant to find where is really small
        // float currScan2RbtDist = 0.0;
        for (int i = 0; i < scan2RbtDists.size(); i++) 
        {
            scan2RbtDists.at(i) = dist2Pose(idx2theta(i), scan.ranges.at(i), pose);
        }

        auto iter = std::min_element(scan2RbtDists.begin(), scan2RbtDists.end());
        // std::cout << "robot pose: " << pose.position.x << ", " << pose.position.y << ")" << std::endl;
        int minDistIdx = std::distance(scan2RbtDists.begin(), iter);
        float range = scan.ranges.at(minDistIdx);
        float theta = idx2theta(minDistIdx);
        float cost = chapterScore(*iter);
        //std::cout << *iter << ", regular cost: " << cost << std::endl;
        ROS_INFO_STREAM_NAMED("TrajectoryScorer", "            robot pose: " << pose.position.x << ", " << pose.position.y << 
                    ", closest scan point: " << range * std::cos(theta) << ", " << range * std::sin(theta) << ", static cost: " << cost);
        return cost;
    }

    float TrajectoryScorer::chapterScore(const float & rbtToScanDist) 
    {
        // if the distance at the pose is less than the inscribed radius of the robot, return negative infinity
        // std::cout << "in chapterScore with distance: " << d << std::endl;
        float inflRbtRad = cfg_->rbt.r_inscr * cfg_->traj.inf_ratio; 

        float inflRbtToScanDist = rbtToScanDist - inflRbtRad;

        if (inflRbtToScanDist < 0.0) 
        {   
            return std::numeric_limits<float>::infinity();
        }

        // if pose is sufficiently far away from scan, return no cost
        if (rbtToScanDist > cfg_->traj.max_pose_to_scan_dist) 
            return 0;

        /*   y
         *   ^
         * Q |\ 
         *   | \
         *   |  \
         *       `
         *   |    ` 
         *         ` ---
         *   |          _________
         *   --------------------->x
         *
         */


        return cfg_->traj.Q * std::exp(-cfg_->traj.pen_exp_weight * inflRbtToScanDist);
    }
}