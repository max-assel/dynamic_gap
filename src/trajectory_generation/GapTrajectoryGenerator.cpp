#include <dynamic_gap/trajectory_generation/GapTrajectoryGenerator.h>

namespace dynamic_gap 
{
    dynamic_gap::Trajectory GapTrajectoryGenerator::generateTrajectory(dynamic_gap::Gap * selectedGap, 
                                                                        const geometry_msgs::PoseStamped & currPose, 
                                                                        const geometry_msgs::TwistStamped & currVel,
                                                                        const geometry_msgs::PoseStamped & globalGoalRobotFrame,
                                                                        const bool & runGoToGoal) 
    {
        ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "        [generateTrajectory()]");

        geometry_msgs::PoseArray path;
        std::vector<float> pathTiming;

        try 
        {        
		    std::chrono::steady_clock::time_point generateTrajectoryStartTime = std::chrono::steady_clock::now();

            path.header.stamp = ros::Time::now();
            TrajectoryLogger logger(path, cfg_->robot_frame_id, pathTiming);
            path.header.frame_id = cfg_->sensor_frame_id;

            Eigen::Vector4f rbtState(currPose.pose.position.x, 
                                     currPose.pose.position.y,
                                     0.0, 
                                     0.0);

            // get gap points in cartesian
            float xLeft = 0.0, yLeft = 0.0, xRight = 0.0, yRight = 0.0;
            selectedGap->getManipulatedLCartesian(xLeft, yLeft);
            selectedGap->getManipulatedRCartesian(xRight, yRight);

            if (runGoToGoal) 
            {
                ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "            running go to goal");                

                robotAndGapState x = {rbtState[0], rbtState[1], 
                                        xLeft, yLeft, 
                                        xRight, yRight, 
                                        globalGoalRobotFrame.pose.position.x, globalGoalRobotFrame.pose.position.y};
                // ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "Goal to Goal");
                GoToGoal goToGoal(cfg_->rbt.vx_absmax);
                boost::numeric::odeint::integrate_const(boost::numeric::odeint::euler<robotAndGapState>(),
                goToGoal, x, 0.0f, cfg_->traj.integrate_maxt, cfg_->traj.integrate_stept, logger);
                dynamic_gap::Trajectory traj(path, pathTiming);
                float generateTrajectoryTime = timeTaken(generateTrajectoryStartTime);
                ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "            generateTrajectory (g2g) time taken: " << generateTrajectoryTime << " seconds");                
                return traj;
            }

            ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "            running pursuit guidance");                

            Eigen::Vector2d initialGoal(selectedGap->goal.x_, selectedGap->goal.y_);
            // Eigen::Vector2d terminalGoal(selectedGap->terminalGoal.x_, selectedGap->terminalGoal.y_);

            // float initialGoalX = initialGoal[0];
            // float initialGoalY = initialGoal[1];
            // float terminalGoalX = terminalGoal[0];
            // float terminalGoalY = terminalGoal[1];

            // float goalVelX = epsilonDivide(terminalGoalX - initialGoalX, selectedGap->gapLifespan_); // absolute velocity (not relative to robot)
            // float goalVelY = epsilonDivide(terminalGoalY - initialGoalY, selectedGap->gapLifespan_);

            selectedGap->leftGapPtModel_->isolateGapDynamics();
            selectedGap->rightGapPtModel_->isolateGapDynamics();

            Eigen::Vector4f leftGapState = selectedGap->leftGapPtModel_->getGapState();
            Eigen::Vector4f rightGapState = selectedGap->rightGapPtModel_->getGapState();

            float leftVelX = leftGapState[2];
            float leftVelY = leftGapState[3];

            float rightVelX = rightGapState[2];
            float rightVelY = rightGapState[3];

            float gapGoalVelX = (leftVelX + rightVelX) / 2.;
            float gapGoalVelY = (leftVelY + rightVelY) / 2.;
            
            ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "            initial robot pos: (" << rbtState[0] << ", " << rbtState[1] << ")");
            ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "            inital robot velocity: " << rbtState[2] << ", " << rbtState[3] << ")");
            ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "            initial left gap point: (" << xLeft << ", " << yLeft << "), initial right point: (" << xRight << ", " << yRight << ")"); 
            ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "            initial left gap point velocity: (" << leftVelX << ", " << leftVelY << "), initial right gap point velocity: (" << rightVelX << ", " << rightVelY << ")"); 
            ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "            initial goal: (" << initialGoal[0] << ", " << initialGoal[1] << ")"); 
            ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "            initial goal velocity: (" << gapGoalVelX << ", " << gapGoalVelY << ")"); 

            // ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "            actual terminal goal: (" << terminalGoalX << ", " << terminalGoalY << ")"); 
            

            // robotAndGapState x = {rbtState[0], rbtState[1], xLeft, yLeft, xRight, yRight, initialGoalX, initialGoalY};
            
            // ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "pre-integration, x: " << x[0] << ", " << x[1] << ", " << x[2] << ", " << x[3]);

            // Eigen::Vector2d initRbtPos(x[0], x[1]);
            // Eigen::Vector2d leftCurveInitPt(xLeft, yLeft);
            // Eigen::Vector2d leftCurveTermPt(xLeftTerm, yLeftTerm);
            // Eigen::Vector2d rightCurveInitPt(xRight, yRight);
            // Eigen::Vector2d rightCurveTermPt(xRightTerm, yRightTerm);
            Eigen::Vector2f leftGapPtVel(leftVelX, leftVelY);
            Eigen::Vector2f rightGapPtVel(rightVelX, rightVelY);
            // Eigen::Vector2d gapGoalTermPt(terminalGoalX, terminalGoalY);
            // Eigen::Vector2d gapGoalVel(goalVelX, goalVelY);
            
            // Eigen::Vector2d maxRbtVel(cfg_->rbt.vx_absmax, cfg_->rbt.vy_absmax);
            // Eigen::Vector2d maxRbtAcc(cfg_->rbt.ax_absmax, cfg_->rbt.ay_absmax);

            robotAndGapState x = {rbtState[0], rbtState[1], xLeft, yLeft, xRight, yRight, initialGoal[0], initialGoal[1]};

            /*
            PolarGapField polarGapField(xRight, xLeft, yRight, yLeft,
                                                    initialGoalX, initialGoalY,
                                                    selectedGap->isRadial(),
                                                    x[0], x[1],
                                                    cfg_->rbt.vx_absmax, cfg_->rbt.vx_absmax);

            // POLAR GAP FIELD
            boost::numeric::odeint::integrate_const(boost::numeric::odeint::euler<robotAndGapState>(),
                                                    polarGapField, x, 0.0f, selectedGap->gapLifespan_, 
                                                    cfg_->traj.integrate_stept, logger);
            for (geometry_msgs::Pose & p : posearr.poses) 
            {
                p.position.x += selectedGap->extendedGapOrigin_[0];
                p.position.y += selectedGap->extendedGapOrigin_[1];
            }
            
            */
            
            ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "            intercept time: " << selectedGap->t_intercept); 
            ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "            intercept angle: " << selectedGap->gamma_intercept); 

            ParallelNavigation parallelNavigation(selectedGap->gamma_intercept, 
                                                    cfg_->rbt.vx_absmax,
                                                    cfg_->rbt.r_inscr,
                                                    leftGapPtVel,
                                                    rightGapPtVel);

            float t_max = std::min(selectedGap->t_intercept, cfg_->traj.integrate_maxt);

            // POLAR GAP FIELD
            boost::numeric::odeint::integrate_const(boost::numeric::odeint::euler<robotAndGapState>(),
                                                    parallelNavigation, x, 0.0f, t_max, 
                                                    cfg_->traj.integrate_stept, logger);

            // boost::numeric::odeint::integrate_const(boost::numeric::odeint::euler<robotAndGapState>(),
            //                                         ahpf, x, 0.0f, selectedGap->gapLifespan_, 
            //                                         cfg_->traj.integrate_stept, logger);

            dynamic_gap::Trajectory traj(path, pathTiming);
            float generateTrajectoryTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - generateTrajectoryStartTime).count() / 1.0e6;
            ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "            generateTrajectory (ahpf) time taken: " << generateTrajectoryTime << " seconds");
            return traj;
            
        } catch (...) 
        {
            ROS_FATAL_STREAM("integrator");
            dynamic_gap::Trajectory traj(path, pathTiming);
            return traj;
        }

    }

    // Transform local trajectory between two frames of choice
    geometry_msgs::PoseArray GapTrajectoryGenerator::transformPath(const geometry_msgs::PoseArray & path,
                                                                              const geometry_msgs::TransformStamped & transform)
    {
        geometry_msgs::PoseStamped sourcePose;
        sourcePose.header.frame_id = transform.header.frame_id; // cfg_->robot_frame_id;

        geometry_msgs::PoseStamped destPose;
        destPose.header.frame_id = transform.child_frame_id; // cfg_->odom_frame_id;

        geometry_msgs::PoseArray transformedPath;
        for (const geometry_msgs::Pose pose : path.poses)
        {
            sourcePose.pose = pose;
            tf2::doTransform(sourcePose, destPose, transform);
            transformedPath.poses.push_back(destPose.pose);
        }
        transformedPath.header.frame_id = destPose.header.frame_id; // cfg_->odom_frame_id;
        transformedPath.header.stamp = ros::Time::now();
        // ROS_WARN_STREAM("leaving transform back with length: " << transformedPath.poses.size());
        return transformedPath;
    }

    dynamic_gap::Trajectory GapTrajectoryGenerator::processTrajectory(const dynamic_gap::Trajectory & traj)
    {
        geometry_msgs::PoseArray rawPath = traj.getPathRbtFrame();
        std::vector<float> rawPathTiming = traj.getPathTiming();
        
        geometry_msgs::Pose originPose;
        originPose.position.x = 0;
        originPose.position.y = 0;
        originPose.position.z = 0;
        originPose.orientation.x = 0;
        originPose.orientation.y = 0;
        originPose.orientation.z = 0;
        originPose.orientation.w = 1;
        // std::cout << "entering at : " << path.poses.size() << std::endl;
        //std::cout << "starting pose: " << posearr.poses[0].position.x << ", " << posearr.poses[0].position.y << std::endl; 
        //std::cout << "final pose: " << posearr.poses[posearr.poses.size() - 1].position.x << ", " << posearr.poses[posearr.poses.size() - 1].position.y << std::endl;

        std::vector<geometry_msgs::Pose> processedPoses;
        std::vector<float> processedPathTiming;
        processedPoses.push_back(originPose);
        processedPathTiming.push_back(0.0);
        // ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "pose[0]: " << path.poses[0].position.x << ", " << path.poses[0].position.y);

        float poseToPoseDistThreshold = 0.1;
        float poseToPoseDiffX = 0.0, poseToPoseDiffY = 0.0, poseToPoseDist = 0.0;
        for (int i = 1; i < rawPath.poses.size(); i++) 
        {
            geometry_msgs::Pose rawPose = rawPath.poses[i];
            poseToPoseDiffX = rawPose.position.x - processedPoses.back().position.x;
            poseToPoseDiffY = rawPose.position.y - processedPoses.back().position.y;
            poseToPoseDist = sqrt(pow(poseToPoseDiffX, 2) + pow(poseToPoseDiffY, 2));
            if (poseToPoseDist > poseToPoseDistThreshold) 
            {
                // ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "poseToPoseDist " << i << " kept at " << poseToPoseDist);
                processedPoses.push_back(rawPose);
                processedPathTiming.push_back(rawPathTiming.at(i));
            } else 
            {
                // ROS_INFO_STREAM_NAMED("GapTrajectoryGenerator", "poseToPoseDist " << i << " cut at " << poseToPoseDist);
            }
        }
        // std::cout << "leaving at : " << shortened.size() << std::endl;
        
        geometry_msgs::PoseArray processedPath = rawPath;        
        processedPath.poses = processedPoses;

        // Fix rotation along local trajectory
        geometry_msgs::Pose processedPose, prevProcessedPose;
        Eigen::Quaternionf q;
        float poseToPoseDiffTheta = 0.0;
        for (int idx = 1; idx < processedPath.poses.size(); idx++)
        {
            processedPose = processedPath.poses.at(idx);
            prevProcessedPose = processedPath.poses.at(idx-1);
            poseToPoseDiffX = processedPose.position.x - prevProcessedPose.position.x;
            poseToPoseDiffY = processedPose.position.y - prevProcessedPose.position.y;
            poseToPoseDiffTheta = std::atan2(poseToPoseDiffY, poseToPoseDiffX);
            q = Eigen::AngleAxisf(0, Eigen::Vector3f::UnitX()) *
                Eigen::AngleAxisf(0, Eigen::Vector3f::UnitY()) *
                Eigen::AngleAxisf(poseToPoseDiffTheta, Eigen::Vector3f::UnitZ());
            q.normalize();
            processedPath.poses.at(idx-1).orientation.x = q.x();
            processedPath.poses.at(idx-1).orientation.y = q.y();
            processedPath.poses.at(idx-1).orientation.z = q.z();
            processedPath.poses.at(idx-1).orientation.w = q.w();
        }
        processedPath.poses.pop_back();
        processedPathTiming.pop_back();

        dynamic_gap::Trajectory processedTrajectory(processedPath, processedPathTiming);
        return processedTrajectory;
    }

}