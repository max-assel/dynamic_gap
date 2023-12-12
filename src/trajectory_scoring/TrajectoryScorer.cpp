#include <dynamic_gap/trajectory_scoring/TrajectoryScorer.h>


namespace dynamic_gap 
{
    TrajectoryScorer::TrajectoryScorer(ros::NodeHandle& nh, const dynamic_gap::DynamicGapConfig& cfg)
    {
        cfg_ = & cfg;
        propagatedEgocirclePublisher_ = nh.advertise<sensor_msgs::LaserScan>("propagated_egocircle", 1);
    }

    void TrajectoryScorer::updateEgoCircle(boost::shared_ptr<sensor_msgs::LaserScan const> scan) 
    {
        boost::mutex::scoped_lock lock(egocircleMutex_);
        scan_ = scan;
    }

    void TrajectoryScorer::updateStaticEgoCircle(const sensor_msgs::LaserScan & staticScan) 
    {
        boost::mutex::scoped_lock lock(egocircleMutex_);
        staticScan_ = staticScan;
    }

    void TrajectoryScorer::transformGlobalPathLocalWaypointToRbtFrame(const geometry_msgs::PoseStamped & globalPathLocalWaypointOdomFrame, 
                                                                      const geometry_msgs::TransformStamped & odom2rbt) 
    {
        boost::mutex::scoped_lock lock(globalPlanMutex_);
        tf2::doTransform(globalPathLocalWaypointOdomFrame, globalPathLocalWaypointRobotFrame_, odom2rbt);
    }

    /*
    bool compareModelBearings(dynamic_gap::RotatingFrameCartesianKalmanFilter* model_one, 
                              dynamic_gap::RotatingFrameCartesianKalmanFilter* model_two) 
    {
        Eigen::Matrix<float, 4, 1> state_one = model_one->getGapState();
        Eigen::Matrix<float, 4, 1> state_two = model_two->getGapState();
        
        return atan2(state_one[1], state_one[0]) < atan2(state_two[1], state_two[0]);
    }
    */

    struct Comparator 
    {
        bool operator() (std::vector<float> & a, std::vector<float> & b) 
        {
            float aNorm = pow(a[0], 2) + pow(a[1], 2);
            float bNorm = pow(b[0], 2) + pow(b[1], 2);
            return aNorm < bNorm;
        }
    };

    std::vector<std::vector<float>> TrajectoryScorer::sortAndPrune(const std::vector<Eigen::Matrix<float, 4, 1> > & agentPoses)
    {
        // Declare vector of pairs
        std::vector< std::vector<float> > A;
        
        // Copy key-value pair from Map
        // to vector of pairs
        float dist;
        for (int i = 0; i < agentPoses.size(); i++) 
        {
            // ROS_INFO_STREAM(it.first);
            //ROS_INFO_STREAM("pose: " << std::to_string(it.second[0]) << ", " << std::to_string(it.second[1]));
            //ROS_INFO_STREAM("ego pose: " << pt1[0] << ", " << pt1[1]);

            std::vector<float> agentPose{agentPoses[i][0], agentPoses[i][1]};
            
            dist = sqrt(pow(agentPose[0], 2) + pow(agentPose[1], 2));
            //ROS_INFO_STREAM("dist: " << dist);
            if (dist < cfg_->scan.range_max) 
            {
                A.push_back(agentPose);
            }
        }
        
        // Sort using comparator function
        sort(A.begin(), A.end(), Comparator());
        
        // ROS_INFO_STREAM("printing pruned vect");
        // Print the sorted value
        /*
        for (auto& it : A) {
    
            ROS_INFO_STREAM(it.first << ' ' << it.second[0] << ", " << it.second[1]);
        }
        */
        

        return A;
    }

    void TrajectoryScorer::recoverDynamicEgoCircle(float t_i, float t_iplus1, 
                                                   std::vector<Eigen::Matrix<float, 4, 1> > & propagatedAgents,
                                                   sensor_msgs::LaserScan & dynamicLaserScan,
                                                   bool print) 
    {   
        if (print) ROS_INFO_STREAM("    [recoverDynamicEgoCircle()]");
        float interval = t_iplus1 - t_i;
        if (interval <= 0.0)
            return;

        if (print) ROS_INFO_STREAM("        recovering dynamic egocircle for interval: " << t_i << " to " << t_iplus1);
        // for EVERY interval, start with static scan
        dynamicLaserScan.ranges = staticScan_.ranges;

        // propagate poses forward (all odoms and vels are in robot frame)
        for (int i = 0; i < propagatedAgents.size(); i++) 
        {
            if (print)
                ROS_INFO_STREAM("        agent" << i << " moving from (" << propagatedAgents[i][0] << ", " << propagatedAgents[i][1] << ")");

            propagatedAgents[i][0] += propagatedAgents[i][2]*interval;
            propagatedAgents[i][1] += propagatedAgents[i][3]*interval;
            
            if (print)
                ROS_INFO_STREAM("                                  to (" << propagatedAgents[i][0] << ", " << propagatedAgents[i][1] << ")");
        }

        // basically running modify_scan                                                          
        Eigen::Vector2d scanPt, centeredAgentPt, centeredScanPt, intersection0, intersection1, 
                        intersection0MinusAgent, intersection0MinusScan, intersection1MinusAgent, intersection1MinusScan, 
                        scanPtMinus;
        float theta, range, deltaX, deltaY, dr, determinant, discriminant, dist0, dist1;
        std::vector<float> agentPose;

        std::vector<std::vector<float>> agentPoses = sortAndPrune(propagatedAgents);

        for (int i = 0; i < dynamicLaserScan.ranges.size(); i++) 
        {
            theta = theta2idx(i); // dynamicLaserScan.angle_min + i*dynamicLaserScan.angle_increment;
            // cout << "i: " << i << " rad: " << rad << endl;
            // cout << "original distance " << modified_laser_scan.ranges[i] << endl;
            dynamicLaserScan.ranges[i] = std::min(dynamicLaserScan.ranges[i], cfg_->scan.range_max);
            // ROS_INFO_STREAM("i: " << i << ", rad: " << rad << ", range: " << dynamicLaserScan.ranges[i]);
            range = dynamicLaserScan.ranges[i];
            
            scanPt << range*cos(theta), range*sin(theta);

            // map<string, vector<float>>::iterator it;
            //vector<pair<string, vector<float> >> odom_vect = sortAndPrune(odom_map);
            // TODO: sort map here according to distance from robot. Then, can break after first intersection
            for (int j = 0; j < agentPoses.size(); j++) 
            {
                agentPose = agentPoses[j];
                // ROS_INFO_STREAM("ODOM MAP SECOND: " << agentPose[0] << ", " << agentPose[1]);
                // int idx_dist = std::distance(odom_map.begin(), it);
                // ROS_INFO_STREAM("EGO ROBOT ODOM: " << pt1[0] << ", " << pt1[1]);
                // ROS_INFO_STREAM("ODOM VECT SECOND: " << agentPose[0] << ", " << agentPose[1]);

                // centered ego robot state
                centeredAgentPt << -agentPose[0], -agentPose[1]; 
                // ROS_INFO_STREAM("centeredAgentPt: " << centeredAgentPt[0] << ", " << centeredAgentPt[1]);

                // static laser scan point
                centeredScanPt << scanPt[0] - agentPose[0], scanPt[1] - agentPose[1]; 
                // ROS_INFO_STREAM("centered_pt2: " << centered_pt2[0] << ", " << centered_pt2[1]);

                deltaX = centeredScanPt[0] - centeredAgentPt[0];
                deltaY = centeredScanPt[1] - centeredAgentPt[1];
                // dx_dy << deltaX, deltaY;
                dr = sqrt(pow(deltaX, 2) + pow(deltaY, 2)); // dx_dy.norm();

                determinant = centeredAgentPt[0]*centeredScanPt[1] - centeredScanPt[0]*centeredAgentPt[1];
                discriminant = pow(cfg_->rbt.r_inscr,2) * pow(dr, 2) - pow(determinant, 2);

                if (discriminant > 0) 
                {
                    intersection0 << (determinant*deltaY + signum(deltaY) * deltaX * sqrt(discriminant)) / pow(dr, 2),
                                     (-determinant * deltaX + abs(deltaY)*sqrt(discriminant)) / pow(dr, 2);
                                        
                    intersection1 << (determinant*deltaY - signum(deltaY) * deltaX * sqrt(discriminant)) / pow(dr, 2),
                                     (-determinant * deltaX - abs(deltaY)*sqrt(discriminant)) / pow(dr, 2);
                    intersection0MinusAgent = intersection0 - centeredAgentPt;
                    intersection1MinusAgent = intersection1 - centeredAgentPt;
                    scanPtMinus = centeredScanPt - centeredAgentPt;

                    dist0 = intersection0MinusAgent.norm();
                    dist1 = intersection1MinusAgent.norm();
                    
                    if (dist0 < dist1) 
                    {
                        intersection0MinusScan = intersection0 - centeredScanPt;

                        if (dist0 < dynamicLaserScan.ranges[i] && dist0 < scanPtMinus.norm() && 
                            intersection0MinusScan.norm() < scanPtMinus.norm()) 
                        {
                            if (print) 
                            {
                                ROS_INFO_STREAM("        at i: " << i << ", changed distance from " << 
                                                                            dynamicLaserScan.ranges[i] << " to " << dist0);
                            }

                            dynamicLaserScan.ranges[i] = dist0;
                            break;
                        }
                    } else 
                    {
                        intersection1MinusScan = intersection1 - centeredScanPt;

                        if (dist1 < dynamicLaserScan.ranges[i] && dist1 < scanPtMinus.norm() && 
                            intersection1MinusScan.norm() < scanPtMinus.norm()) 
                        {
                            if (print) 
                            {
                                ROS_INFO_STREAM("        at i: " << i << ", changed distance from " << 
                                                dynamicLaserScan.ranges[i] << " to " << dist1); 
                            }          

                            dynamicLaserScan.ranges[i] = dist1;
                            break;
                        }
                    }
                }
            }
        }
    }

    // Does things in rbt frame
    /*
    std::vector<float> TrajectoryScorer::scoreGaps()
    {
        boost::mutex::scoped_lock planlock(globalPlanMutex_);
        boost::mutex::scoped_lock egolock(egocircleMutex_);
        if (gaps.size() < 1) {
            //ROS_WARN_STREAM("Observed num of gap: 0");
            return std::vector<float>(0);
        }

        // How fix this
        int num_of_scan = scan_.get()->ranges.size();
        float goal_orientation = std::atan2(globalPathLocalWaypointRobotFrame_.pose.position.y, globalPathLocalWaypointRobotFrame_.pose.position.x);
        int idx = goal_orientation / (M_PI / (num_of_scan / 2)) + (num_of_scan / 2);
        ROS_DEBUG_STREAM("Goal Orientation: " << goal_orientation << ", idx: " << idx);
        ROS_DEBUG_STREAM(globalPathLocalWaypointRobotFrame_.pose.position);
        auto costFn = [](dynamic_gap::Gap g, int goal_idx) -> float
        {
            int leftdist = std::abs(g.RIdx() - goal_idx);
            int rightdist = std::abs(g.LIdx() - goal_idx);
            return std::min(leftdist, rightdist);
        };

        std::vector<float> cost(gaps.size());
        for (int i = 0; i < cost.size(); i++) 
        {
            cost.at(i) = costFn(gaps.at(i), idx);
        }

        return cost;
    }
    */

    void TrajectoryScorer::visualizePropagatedEgocircle(const sensor_msgs::LaserScan & dynamicLaserScan) 
    {
        propagatedEgocirclePublisher_.publish(dynamicLaserScan);
    }

    std::vector<float> TrajectoryScorer::scoreTrajectory(const geometry_msgs::PoseArray & path, 
                                                         const std::vector<float> & pathTiming, 
                                                         const std::vector<dynamic_gap::Gap> & rawGaps,
                                                         const std::vector<sensor_msgs::LaserScan> & futureScans) 
    {    
        if (cfg_->debug.traj_debug_log) ROS_INFO_STREAM("         [scoreTrajectory()]");
        // Requires LOCAL FRAME
        // Should be no racing condition
        // float start_time = ros::WallTime::now().toSec();

        /*
        std::vector<dynamic_gap::Estimator *> raw_models;
        for (auto gap : rawGaps) {
            raw_models.push_back(gap.rightGapPtModel_);
            raw_models.push_back(gap.leftGapPtModel_);
        }
        
        
        // std::cout << "starting setting sides and freezing velocities" << std::endl;
        for (auto & model : raw_models) {
            model->isolateGapDynamics();
        }
        */
        
        float totalTrajCost = 0.0;
        std::vector<float> posewiseCosts;

        
        sensor_msgs::LaserScan dynamicLaserScan = sensor_msgs::LaserScan();

        float t_i = 0.0;
        float t_iplus1 = 0.0;
        // int counts = std::min(cfg_->planning.num_feasi_check, int(path.poses.size()));        

        /*
        int min_dist_idx, future_scan_idx;
        float theta, range;
        Eigen::Vector2d min_dist_pt(0.0, 0.0);
        if (rawGaps.size() > 0) {
            std::vector<float> dynamic_cost_val(path.poses.size());
            for (int i = 0; i < dynamic_cost_val.size(); i++) {
                // std::cout << "regular range at " << i << ": ";
                t_iplus1 = pathTiming[i];
                future_scan_idx = (int) (t_iplus1 / cfg_->traj.integrate_stept);
                // ROS_INFO_STREAM("pulling scan for t_iplus1: " << t_iplus1 << " at idx: " << future_scan_idx);
                dynamicLaserScan = futureScans[future_scan_idx];
                // ROS_INFO_STREAM("pulled scan");
                min_dist_idx = dynamicGetMinDistIndex(path.poses.at(i), dynamicLaserScan, print);
                // ROS_INFO_STREAM("min_dist_idx: " << min_dist_idx);
                // add point to min_dist_array
                theta = min_dist_idx * dynamicLaserScan.angle_increment + dynamicLaserScan.angle_min;
                range = dynamicLaserScan.ranges.at(min_dist_idx);
                min_dist_pt << range*std::cos(theta), range*std::sin(theta);
 
                
                // if (t_iplus1 == 2.5 && vis) 
                // {
                //     ROS_INFO_STREAM("visualizing dynamic egocircle from " << t_i << " to " << t_iplus1);
                //     visualizePropagatedEgocircle(dynamicLaserScan); // if I do i ==0, that's just original scan
                // }
                


                // get cost of point
                dynamic_cost_val.at(i) = dynamicScorePose(path.poses.at(i), theta, range);
                // ROS_INFO_STREAM("dynamic_cost_val: " << dynamic_cost_val.at(i));
                
                if (dynamic_cost_val.at(i) < 0.0 && cfg_->debug.traj_debug_log) {
                    ROS_INFO_STREAM("at pose: " << i << " of " << dynamic_cost_val.size() << 
                                    "(t: " << t_iplus1 << "), score of: " << 
                                    dynamic_cost_val.at(i) << ", robot pose: " << 
                                    path.poses.at(i).position.x << ", " << path.poses.at(i).position.y << 
                                    ", closest point: " << min_dist_pt[0] << ", " << min_dist_pt[1] << ", distance: " << dist2Pose(theta, range, path.poses.at(i)));
                }
                
                
                t_i = t_iplus1;
            }

            totalTrajCost = std::accumulate(dynamic_cost_val.begin(), dynamic_cost_val.end(), float(0));
            cost_val = dynamic_cost_val;
            if (cfg_->debug.traj_debug_log) ROS_INFO_STREAM("dynamic pose-wise cost: " << totalTrajCost);
        } else {
        */

        std::vector<float> staticPosewiseCosts(path.poses.size());
        for (int i = 0; i < staticPosewiseCosts.size(); i++) 
        {
            // std::cout << "regular range at " << i << ": ";
            staticPosewiseCosts.at(i) = scorePose(path.poses.at(i)); //  / staticPosewiseCosts.size()
        }
        totalTrajCost = std::accumulate(staticPosewiseCosts.begin(), staticPosewiseCosts.end(), float(0));
        if (cfg_->debug.traj_debug_log) ROS_INFO_STREAM("             static pose-wise cost: " << totalTrajCost);
        // }


        posewiseCosts = staticPosewiseCosts;
        if (posewiseCosts.size() > 0) 
        {
            // obtain terminalGoalCost, scale by w1
            float w1 = 1.0;
            float terminalCost = w1 * terminalGoalCost(*std::prev(path.poses.end()));
            // if the ending cost is less than 1 and the total cost is > -10, return trajectory of 100s
            if (terminalCost < 0.25 && totalTrajCost >= 0) 
            {
                // std::cout << "returning really good trajectory" << std::endl;
                return std::vector<float>(path.poses.size(), 100);
            }
            // Should be safe, subtract terminal pose cost from first pose cost
            if (cfg_->debug.traj_debug_log) ROS_INFO_STREAM("            terminal cost: " << -terminalCost);
            posewiseCosts.at(0) -= terminalCost;
        }
        
        // ROS_INFO_STREAM("scoreTrajectory time taken:" << ros::WallTime::now().toSec() - start_time);
        return posewiseCosts;
    }

    float TrajectoryScorer::terminalGoalCost(const geometry_msgs::Pose & pose) 
    {
        boost::mutex::scoped_lock planlock(globalPlanMutex_);
        // ROS_INFO_STREAM(pose);
        if (cfg_->debug.traj_debug_log) ROS_INFO_STREAM("            final pose: (" << pose.position.x << ", " << pose.position.y << "), local goal: (" << globalPathLocalWaypointRobotFrame_.pose.position.x << ", " << globalPathLocalWaypointRobotFrame_.pose.position.y << ")");
        float dx = pose.position.x - globalPathLocalWaypointRobotFrame_.pose.position.x;
        float dy = pose.position.y - globalPathLocalWaypointRobotFrame_.pose.position.y;
        return sqrt(pow(dx, 2) + pow(dy, 2));
    }

    // if we wanted to incorporate how egocircle can change, 
    float TrajectoryScorer::dist2Pose(float theta, float range, const geometry_msgs::Pose & pose) 
    {
        // ego circle point in local frame, pose in local frame
        // ROS_INFO_STREAM("   theta: " << theta << ", range: " << range);
        // ROS_INFO_STREAM("   rbt_x: " << pose.position.x << ", rbt_y: " << pose.position.y);
        float x = range * std::cos(theta);
        float y = range * std::sin(theta);
        float dist = sqrt(pow(pose.position.x - x, 2) + pow(pose.position.y - y, 2)); 
        // ROS_INFO_STREAM("   dist: " << dist);
        return dist;
    }

    /*
    int TrajectoryScorer::dynamicGetMinDistIndex(const geometry_msgs::Pose & pose, 
                                                 const sensor_msgs::LaserScan & dynamicLaserScan, 
                                                 bool print) 
    {
        boost::mutex::scoped_lock lock(egocircleMutex_);

        // obtain orientation and idx of pose
        // float poseTheta = std::atan2(pose.position.y + 1e-3, pose.position.x + 1e-3);
        // int center_idx = theta2idx(poseTheta); // (int) std::round((pose_ori + M_PI) / scan_.get()->angle_increment);
        
        // int scan_size = (int) ;
        // dist is size of scan
        std::vector<float> scan2RbtDists(dynamicLaserScan.ranges.size());

        // This size **should** be ensured
        // if (dynamicLaserScan.ranges.size() < 500) {
        //     ROS_FATAL_STREAM("Scan range incorrect scorePose");
        // }

        // iterate through ranges and obtain the distance from the egocircle point and the pose
        // Meant to find where is really small
        float currScan2RbtDist;
        for (int i = 0; i < scan2RbtDists.size(); i++) 
        {
            currScan2RbtDist = dynamicLaserScan.ranges.at(i);
            // currScan2RbtDist = (currScan2RbtDist == 5) ? currScan2RbtDist + cfg_->traj.rmax : currScan2RbtDist; // WHY IS THIS HAPPENING
            scan2RbtDists.at(i) = dist2Pose(idx2theta(i), currScan2RbtDist, pose); // i * dynamicLaserScan.angle_increment - M_PI
        }

        auto iter = std::min_element(scan2RbtDists.begin(), scan2RbtDists.end());
        // int min_dist_index = 
        return std::distance(scan2RbtDists.begin(), iter);
    }
    */
    
    float TrajectoryScorer::dynamicScorePose(const geometry_msgs::Pose & pose, float theta, float range) 
    {
        float dist = dist2Pose(theta, range, pose);
        float cost = dynamicChapterScore(dist);

        return cost;
    }

    float TrajectoryScorer::scorePose(const geometry_msgs::Pose & pose) 
    {
        boost::mutex::scoped_lock lock(egocircleMutex_);
        sensor_msgs::LaserScan scan = *scan_.get();

        // obtain orientation and idx of pose
        //float pose_ori = std::atan2(pose.position.y + 1e-3, pose.position.x + 1e-3);
        //int center_idx = (int) std::round((pose_ori + M_PI) / msg.get()->angle_increment);
        
        // int scan_size = (int) ;
        // dist is size of scan
        std::vector<float> scan2RbtDists(scan.ranges.size());

        // This size **should** be ensured
        // if (scan.ranges.size() < 500) {
        //     ROS_FATAL_STREAM("Scan range incorrect scorePose");
        // }

        // iterate through ranges and obtain the distance from the egocircle point and the pose
        // Meant to find where is really small
        float currScan2RbtDist;
        for (int i = 0; i < scan2RbtDists.size(); i++) 
        {
            currScan2RbtDist = scan.ranges.at(i);
            // currScan2RbtDist = currScan2RbtDist == 5 ? currScan2RbtDist + cfg_->traj.rmax : currScan2RbtDist; // WHY IS THIS HAPPENING
            scan2RbtDists.at(i) = dist2Pose(idx2theta(i), currScan2RbtDist, pose); // i * scan.angle_increment - M_PI
        }

        auto iter = std::min_element(scan2RbtDists.begin(), scan2RbtDists.end());
        // std::cout << "robot pose: " << pose.position.x << ", " << pose.position.y << ")" << std::endl;
        int minDistIdx = std::distance(scan2RbtDists.begin(), iter);
        float range = scan.ranges.at(minDistIdx);
        float theta = idx2theta(minDistIdx); // std::distance(scan2RbtDists.begin(), iter) * scan.angle_increment - M_PI;
        float cost = chapterScore(*iter);
        //std::cout << *iter << ", regular cost: " << cost << std::endl;
        ROS_INFO_STREAM("            robot pose: " << pose.position.x << ", " << pose.position.y << 
                    ", closest scan point: " << range * std::cos(theta) << ", " << range * std::sin(theta) << ", static cost: " << cost);
        return cost;
    }

    float TrajectoryScorer::dynamicChapterScore(float d) 
    {
        // if the ditance at the pose is less than the inscribed radius of the robot, return negative infinity
        // std::cout << "in chapterScore with distance: " << d << std::endl;
        float inflRbtRad = cfg_->rbt.r_inscr * cfg_->traj.inf_ratio; 

        if (d < inflRbtRad) 
        { //  
            // std::cout << "distance: " << d << ", r_inscr * inf_ratio: " << r_inscr * cfg_->traj.inf_ratio << std::endl;
            return -std::numeric_limits<float>::infinity();
        }
        // if distance is beyond scan, return 0
        if (d > cfg_->traj.max_pose_pen_dist) 
            return 0;

        return cfg_->traj.cobs * std::exp(-cfg_->traj.pose_exp_weight * (d - inflRbtRad));
    }

    float TrajectoryScorer::chapterScore(float d) 
    {
        // if the ditance at the pose is less than the inscribed radius of the robot, return negative infinity
        // std::cout << "in chapterScore with distance: " << d << std::endl;
        float inflRbtRad = cfg_->rbt.r_inscr * cfg_->traj.inf_ratio; 

        if (d < inflRbtRad) 
        { //   
            // std::cout << "distance: " << d << ", r_inscr * inf_ratio: " << r_inscr * cfg_->traj.inf_ratio << std::endl;
            return -std::numeric_limits<float>::infinity();
        }
        // if distance is essentially infinity, return 0
        if (d > cfg_->traj.max_pose_pen_dist) 
            return 0;

        return cfg_->traj.cobs * std::exp(-cfg_->traj.pose_exp_weight * (d - inflRbtRad));
    }
}