
#include <dynamic_gap/gap_feasibility.h>

namespace dynamic_gap {

    void GapFeasibilityChecker::updateEgoCircle(boost::shared_ptr<sensor_msgs::LaserScan const> msg_) {
        boost::mutex::scoped_lock lock(egolock);
        msg = msg_;
        num_of_scan = (int)(msg.get()->ranges.size());
    }

    bool GapFeasibilityChecker::indivGapFeasibilityCheck(dynamic_gap::Gap& gap) {
        // std::cout << "FEASIBILITY CHECK" << std::endl;
        bool feasible;
        auto half_num_scan = gap.half_scan;
        float x_r_pov, x_l_pov, y_r_pov, y_l_pov;

        x_r_pov = gap.cvx_RDistPOV() * cos(((float) gap.cvx_RIdxPOV() - gap.half_scan) / gap.half_scan * M_PI);
        y_r_pov = gap.cvx_RDistPOV() * sin(((float) gap.cvx_RIdxPOV() - gap.half_scan) / gap.half_scan * M_PI);
        x_l_pov = gap.cvx_LDistPOV() * cos(((float) gap.cvx_LIdxPOV() - gap.half_scan) / gap.half_scan * M_PI);
        y_l_pov = gap.cvx_LDistPOV() * sin(((float) gap.cvx_LIdxPOV() - gap.half_scan) / gap.half_scan * M_PI);
            
        Eigen::Vector2f pl(x_r_pov, y_r_pov);
        Eigen::Vector2f pr(x_l_pov, y_l_pov);
        Eigen::Vector2f pg = (pl + pr) / 2.0;

        // FLIPPING MODELS HERE
        //std::cout << "FLIPPING MODELS TO GET L/R FROM ROBOT POV" << std::endl;
        dynamic_gap::cart_model* left_model_pov = gap.left_model_pov;
        dynamic_gap::cart_model* right_model_pov = gap.right_model_pov;

        // FEASIBILITY CHECK
        // std::cout << "left ori: " << left_ori << ", right_ori: " << right_ori << std::endl;
        //std::cout << "left idx: " << gap.convex.convex_lidx << ", right idx: " << gap.convex.convex_ridx << std::endl;
        double start_time = ros::Time::now().toSec();
        feasible = feasibilityCheck(gap, left_model_pov, right_model_pov);
        // ROS_INFO_STREAM("feasibilityCheck time elapsed:" << ros::Time::now().toSec() - start_time);
        ROS_INFO_STREAM("is gap feasible: " << feasible);
        return feasible;
    }

    bool GapFeasibilityChecker::feasibilityCheck(dynamic_gap::Gap& gap, dynamic_gap::cart_model* left_model_pov, dynamic_gap::cart_model* right_model_pov) {
        bool feasible = false;

        left_model_pov->freeze_robot_vel();
        right_model_pov->freeze_robot_vel();

        Matrix<double, 4, 1> frozen_left_model_pov_state = left_model_pov->get_frozen_modified_polar_state();
        Matrix<double, 4, 1> frozen_right_model_pov_state = right_model_pov->get_frozen_modified_polar_state();

        double frozen_left_betadot = frozen_left_model_pov_state[3];
        double frozen_right_betadot = frozen_right_model_pov_state[3];

        ROS_INFO_STREAM("frozen left betadot: " << frozen_left_betadot);
        ROS_INFO_STREAM("frozen right betadot: " << frozen_right_betadot);

        // double start_time = ros::Time::now().toSec();
        double crossing_time = gapSplinecheck(gap, left_model_pov, right_model_pov);
        // ROS_INFO_STREAM("gapSplinecheck time elapsed:" << ros::Time::now().toSec() - start_time);

        double min_betadot = std::min(frozen_left_betadot, frozen_right_betadot);
        double subtracted_left_betadot = frozen_left_betadot - min_betadot;
        double subtracted_right_betadot = frozen_right_betadot - min_betadot;

        if (gap.artificial) {
            feasible = true;
            gap.gap_lifespan = cfg_->traj.integrate_maxt;
            gap.setTerminalPoints(gap.RIdxPOV(), gap.RDistPOV(), gap.LIdxPOV(), gap.LDistPOV());
        } else if (subtracted_left_betadot > 0) {
            // expanding
            ROS_INFO_STREAM("gap is expanding");
            feasible = true;
            gap.gap_lifespan = cfg_->traj.integrate_maxt;
            gap.setCategory("expanding");
        } else if (subtracted_left_betadot == 0 && subtracted_right_betadot == 0) {
            // static
            ROS_INFO_STREAM("gap is static");
            feasible = true;
            gap.gap_lifespan = cfg_->traj.integrate_maxt;
            gap.setCategory("static");
        } else {
            ROS_INFO_STREAM("gap is closing");
            if (crossing_time >= 0) {
                feasible = true;
                gap.gap_lifespan = crossing_time;
            }
            gap.setCategory("closing");
        }

        return feasible;
    }
    

    double GapFeasibilityChecker::gapSplinecheck(dynamic_gap::Gap & gap, dynamic_gap::cart_model* left_model_pov, dynamic_gap::cart_model* right_model_pov) {

        Eigen::Vector2f crossing_pt(0.0, 0.0);
        double start_time = ros::Time::now().toSec();
        double crossing_time = indivGapFindCrossingPoint(gap, crossing_pt, left_model_pov, right_model_pov);
        // ROS_INFO_STREAM("indivGapFindCrossingPoint time elapsed:" << ros::Time::now().toSec() - start_time);

        Eigen::Vector2f starting_pos(0.0, 0.0);
        Eigen::Vector2f starting_vel(left_model_pov->get_v_ego()[0], left_model_pov->get_v_ego()[1]);
        
        Eigen::Vector2f ending_vel(0.0, 0.0);
        
        if (crossing_pt.norm() > 0) {
            ending_vel << starting_vel.norm() * crossing_pt[0] / crossing_pt.norm(), starting_vel.norm() * crossing_pt[1] / crossing_pt.norm();
        } 
        
        // ROS_INFO_STREAM("starting x: " << starting_pos[0] << ", " << starting_pos[1] << ", " << starting_vel[0] << ", " << starting_vel[1]);
        // ROS_INFO_STREAM("ending x: " << crossing_pt[0] << ", " << crossing_pt[1] << ", ending_vel: " << ending_vel[0] << ", " << ending_vel[1]);
        
        start_time = ros::Time::now().toSec();
        Eigen::MatrixXf A_x = MatrixXf::Random(4,4);
        Eigen::VectorXf b_x = VectorXf::Random(4);
        A_x << 1.0, 0.0, 0.0, 0.0,
             0.0, 1.0, 0.0, 0.0,
             1.0, crossing_time, pow(crossing_time,2), pow(crossing_time,3),
             0.0, 1.0, 2*crossing_time, 3*pow(crossing_time,2);
        //std::cout << "A_x: " << A_x << std::endl;
        b_x << starting_pos[0], starting_vel[0], crossing_pt[0], ending_vel[0];
        //std::cout << "b_x: " << b_x << std::endl;
        Eigen::Vector4f coeffs = A_x.partialPivLu().solve(b_x);
        // std::cout << "x coeffs: " << coeffs[0] << ", " << coeffs[1] << ", " << coeffs[2] << ", " << coeffs[3] << std::endl;
        double peak_velocity_x = 3*coeffs[3]*pow(crossing_time/2.0, 2) + 2*coeffs[2]*crossing_time/2.0 + coeffs[1];
        /*
        double vel_x;
        for (double t = 0.0; t < crossing_time; t += (crossing_time / 15)) {
            vel_x = 3*coeffs[3]*pow(t, 2) + 2*coeffs[2]*t + coeffs[1];
            ROS_INFO_STREAM("at time " << t << ", x velocity is: " << vel_x);
        }
        */        
        
        Eigen::MatrixXf A_y = MatrixXf::Random(4,4);
        Eigen::VectorXf b_y = VectorXf::Random(4);
        A_y << 1.0, 0.0, 0.0, 0.0,
             0.0, 1.0, 0.0, 0.0,
             1.0, crossing_time, pow(crossing_time,2), pow(crossing_time,3),
             0.0, 1.0, 2*crossing_time, 3*pow(crossing_time,2);
        b_y << starting_pos[1], starting_vel[1], crossing_pt[1], ending_vel[1];
        //std::cout << "A_y: " << A_y << std::endl;
        //std::cout << "b_y: " << b_y << std::endl;
        
        coeffs = A_y.partialPivLu().solve(b_y);
        //std::cout << "y coeffs: " << coeffs[0] << ", " << coeffs[1] << ", " << coeffs[2] << ", " << coeffs[3] << std::endl;
        double peak_velocity_y = 3*coeffs[3]*pow(crossing_time/2.0, 2) + 2*coeffs[2]*crossing_time/2.0 + coeffs[1];
        // ROS_INFO_STREAM("spline build time elapsed:" << ros::Time::now().toSec() - start_time);

        /*
        double vel_y;
        for (double t = 0.0; t < crossing_time; t += (crossing_time / 15)) {
            vel_y = 3*coeffs[3]*pow(t, 2) + 2*coeffs[2]*t + coeffs[1];
            ROS_INFO_STREAM("at time " << t << ", y velocity is: " << vel_x);
        }
        */
        ROS_INFO_STREAM("peak velocity: " << peak_velocity_x << ", " << peak_velocity_y);
        gap.peak_velocity_x = peak_velocity_x;
        gap.peak_velocity_y = peak_velocity_y;
        
        if (std::max(std::abs(peak_velocity_x), std::abs(peak_velocity_y)) <= cfg_->control.vx_absmax) {
            return crossing_time;
        } else {
            return -1.0;
        }
    }
 
    double GapFeasibilityChecker::indivGapFindCrossingPoint(dynamic_gap::Gap & gap, Eigen::Vector2f& gap_crossing_point, dynamic_gap::cart_model* left_model_pov, dynamic_gap::cart_model* right_model_pov) {
        //std::cout << "determining crossing point" << std::endl;
        auto egocircle = *msg.get();

        double x_r_pov, x_l_pov, y_r_pov, y_l_pov;

        x_r_pov = (gap.RDistPOV()) * cos(-((double) gap.half_scan - gap.RIdxPOV()) / gap.half_scan * M_PI);
        y_r_pov = (gap.RDistPOV()) * sin(-((double) gap.half_scan - gap.RIdxPOV()) / gap.half_scan * M_PI);

        x_l_pov = (gap.LDistPOV()) * cos(-((double) gap.half_scan - gap.LIdxPOV()) / gap.half_scan * M_PI);
        y_l_pov = (gap.LDistPOV()) * sin(-((double) gap.half_scan - gap.LIdxPOV()) / gap.half_scan * M_PI);
       
        Eigen::Vector2d left_bearing_vect(x_l_pov / gap.LDistPOV(), y_l_pov / gap.LDistPOV());
        Eigen::Vector2d right_bearing_vect(x_r_pov / gap.RDistPOV(), y_r_pov / gap.RDistPOV());

        double L_to_R_angle = getLeftToRightAngle(left_bearing_vect, right_bearing_vect);

        double beta_left = atan2(y_l_pov, x_l_pov); // std::atan2(left_frozen_state[1], left_frozen_state[2]);
        double beta_right = atan2(y_r_pov, x_r_pov); // std::atan2(right_frozen_state[1], right_frozen_state[2]);
        double beta_center = (beta_left - (L_to_R_angle / 2.0));

        Matrix<double, 2, 1> central_bearing_vect(std::cos(beta_center), std::sin(beta_center));
        
        //std::cout << "initial beta left: (" << left_bearing_vect[0] << ", " << left_bearing_vect[1] << "), initial beta right: (" << right_bearing_vect[0] << ", " << right_bearing_vect[1] << "), initial beta center: (" << central_bearing_vect[0] << ", " << central_bearing_vect[1] << ")" << std::endl;
        
        Matrix<double, 4, 1> left_pov_frozen_state = left_model_pov->get_frozen_modified_polar_state();        
        Matrix<double, 4, 1> right_pov_frozen_state = right_model_pov->get_frozen_modified_polar_state();
        Matrix<double, 4, 1> left_pov_frozen_cartesian_state = left_model_pov->get_frozen_cartesian_state();
        Matrix<double, 4, 1> right_pov_frozen_cartesian_state = right_model_pov->get_frozen_cartesian_state();

        ROS_INFO_STREAM("starting frozen cartesian left: " << left_pov_frozen_cartesian_state[0] << ", " << left_pov_frozen_cartesian_state[1] << ", " << left_pov_frozen_cartesian_state[2] << ", " << left_pov_frozen_cartesian_state[3]); 
        ROS_INFO_STREAM("starting frozen cartesian right: " << right_pov_frozen_cartesian_state[0] << ", " << right_pov_frozen_cartesian_state[1] << ", " << right_pov_frozen_cartesian_state[2] << ", " << right_pov_frozen_cartesian_state[3]);

        // double beta_left, beta_right, beta_center;
       
        double left_central_dot, right_central_dot;
        bool first_cross = true;
        bool bearing_crossing_check, range_closing_check;    
        int idx_left, idx_right;
        double min_dist;

        Matrix<double, 4, 1> prev_left_pov_frozen_state = left_pov_frozen_state;        
        Matrix<double, 4, 1> prev_right_pov_frozen_state = right_pov_frozen_state;        
        Matrix<double, 2, 1> prev_central_bearing_vect = central_bearing_vect;

        Eigen::Vector2d left_cross_pt;
        Eigen::Vector2d right_cross_pt;
        for (double t = cfg_->traj.integrate_stept; t < cfg_->traj.integrate_maxt; t += cfg_->traj.integrate_stept) {
            left_model_pov->frozen_state_propagate(cfg_->traj.integrate_stept);
            right_model_pov->frozen_state_propagate(cfg_->traj.integrate_stept);
            // ROS_INFO_STREAM("t: " << t);
            left_pov_frozen_state = left_model_pov->get_frozen_modified_polar_state();
            right_pov_frozen_state = right_model_pov->get_frozen_modified_polar_state();
            // ROS_INFO_STREAM("frozen left MP state: " << left_frozen_state[0] << ", " << left_frozen_state[1] << ", " << left_frozen_state[2] << ", " << left_frozen_state[3]); 
            // ROS_INFO_STREAM("frozen right MP state: " << right_frozen_state[0] << ", " << right_frozen_state[1] << ", " << right_frozen_state[2] << ", " << right_frozen_state[3]);
            beta_left = left_pov_frozen_state[1]; // std::atan2(left_frozen_state[1], left_frozen_state[2]);
            beta_right = right_pov_frozen_state[1]; // std::atan2(right_frozen_state[1], right_frozen_state[2]);
            idx_left = (beta_left - egocircle.angle_min) / egocircle.angle_increment;
            idx_right = (beta_right - egocircle.angle_min) / egocircle.angle_increment;
            // ROS_INFO_STREAM("egocircle.angle_min: " << egocircle.angle_min << ", egocircle.angle_increment: " << egocircle.angle_increment);
            // ROS_INFO_STREAM("beta_left: " << beta_left << ", beta_right: " << beta_right);
            // ROS_INFO_STREAM("idx_left: " << idx_left << ", idx_right: " << idx_right);
            left_bearing_vect << std::cos(beta_left), std::sin(beta_left);
            right_bearing_vect << std::cos(beta_right), std::sin(beta_right);
            L_to_R_angle = getLeftToRightAngle(left_bearing_vect, right_bearing_vect);
            beta_center = (beta_left - 0.5 * L_to_R_angle);

            Matrix<double, 2, 1> central_bearing_vect(std::cos(beta_center), std::sin(beta_center));
        
            left_central_dot = left_bearing_vect.dot(prev_central_bearing_vect);
            right_central_dot = right_bearing_vect.dot(prev_central_bearing_vect);
            bearing_crossing_check = left_central_dot > 0.0 && right_central_dot > 0.0;

            //std::cout << "left bearing vect: " << left_bearing_vect[0] << ", " << left_bearing_vect[1] << std::endl;
            //std::cout << "right bearing vect: " << right_bearing_vect[0] << ", " << right_bearing_vect[1] << std::endl;
            //std::cout << "central bearing vect: " << central_bearing_vect[0] << ", " << central_bearing_vect[1] << std::endl;
            //std::cout << "left/center dot: " << left_central_dot << ", right/center dot: " << right_central_dot << std::endl;
            //std::cout << "sweep dt: " << dt << ", swept check: " << swept_check << ". left beta: " << beta_left << ", left range: " << 1.0 / left_frozen_state[0] << ", right beta: " << beta_right << ", right range: " << 1.0 / right_frozen_state[0] << std::endl;
            if (L_to_R_angle > M_PI && bearing_crossing_check) {
                //left_frozen_cartesian_state = left_model->get_frozen_cartesian_state();
                //right_frozen_cartesian_state = right_model->get_frozen_cartesian_state();
                left_cross_pt << (1.0 / prev_left_pov_frozen_state[0])*std::cos(prev_left_pov_frozen_state[1]), 
                                    (1.0 / prev_left_pov_frozen_state[0])*std::sin(prev_left_pov_frozen_state[1]);
                right_cross_pt << (1.0 / prev_right_pov_frozen_state[0])*std::cos(prev_right_pov_frozen_state[1]), 
                                    (1.0 / prev_right_pov_frozen_state[0])*std::sin(prev_right_pov_frozen_state[1]);

                range_closing_check = std::sqrt(pow(left_cross_pt[0] - right_cross_pt[0], 2) + pow(left_cross_pt[1] - right_cross_pt[1], 2)) 
                                        < 4*cfg_->rbt.r_inscr * cfg_->traj.inf_ratio;
                if (range_closing_check) {
                    ROS_INFO_STREAM("gap closes at " << t << ", left point at: " << left_cross_pt[0] << ", " << left_cross_pt[1] << ", right point at " << right_cross_pt[0] << ", " << right_cross_pt[1]); 
                    if (left_cross_pt.norm() < right_cross_pt.norm()) {
                        //std::cout << "setting right equal to cross" << std::endl;
                        //std::cout << "right state: " << right_frozen_state[0] << ", " << right_frozen_state[1] << ", " << right_frozen_state[2] << std::endl;
                        gap_crossing_point << right_cross_pt[0], right_cross_pt[1];
                    } else {
                        //std::cout << "setting left equal to cross" << std::endl;
                        //std::cout << "left state: " << left_frozen_state[0] << ", " << left_frozen_state[1] << ", " << left_frozen_state[2] << std::endl;
                        gap_crossing_point << left_cross_pt[0], left_cross_pt[1];
                    }

                    gap_crossing_point += 2 * cfg_->rbt.r_inscr * cfg_->traj.inf_ratio * (gap_crossing_point / gap_crossing_point.norm());
                    gap.setClosingPoint(gap_crossing_point[0], gap_crossing_point[1]);
                    generateTerminalPoints(gap, prev_left_pov_frozen_state[1], prev_left_pov_frozen_state[0], prev_right_pov_frozen_state[1], prev_right_pov_frozen_state[0]);

                    gap.gap_closed = true;
                    return t;
                } else {
                    // I think crossing may still be a little fraught. More so at trajectory generation 
                    // level, but how do we ensure that robot abides by both sides and then once gap
                    // crosses, robot then ignores crossing side
                    if (first_cross) {
                        double mid_x = (left_cross_pt[0] + right_cross_pt[0]) / 2;
                        double mid_y = (left_cross_pt[1] + right_cross_pt[1]) / 2;
                        ROS_INFO_STREAM("gap crosses but does not close at " << t << ", left point at: " << left_cross_pt[0] << ", " << left_cross_pt[1] << ", right point at " << right_cross_pt[0] << ", " << right_cross_pt[1]); 
                        gap.setCrossingPoint(mid_x, mid_y);
                        first_cross = false;

                        generateTerminalPoints(gap, prev_left_pov_frozen_state[1], prev_left_pov_frozen_state[0], prev_right_pov_frozen_state[1], prev_right_pov_frozen_state[0]);

                        gap.gap_crossed = true;
                    }
                }
            }

            prev_left_pov_frozen_state = left_pov_frozen_state;
            prev_right_pov_frozen_state = right_pov_frozen_state;
            prev_central_bearing_vect = central_bearing_vect;
        }

        if (!gap.gap_crossed && !gap.gap_closed) {
            left_pov_frozen_state = left_model_pov->get_frozen_modified_polar_state();
            right_pov_frozen_state = right_model_pov->get_frozen_modified_polar_state();
            left_cross_pt << (1.0 / left_pov_frozen_state[0])*std::cos(left_pov_frozen_state[1]), (1.0 / left_pov_frozen_state[0])*std::sin(left_pov_frozen_state[1]);
            right_cross_pt << (1.0 / right_pov_frozen_state[0])*std::cos(right_pov_frozen_state[1]), (1.0 / right_pov_frozen_state[0])*std::sin(right_pov_frozen_state[1]);
            ROS_INFO_STREAM("no close, final swept points at: (" << left_cross_pt[0] << ", " << left_cross_pt[1] << "), (" << right_cross_pt[0] << ", " << right_cross_pt[1] << ")");

            generateTerminalPoints(gap, left_pov_frozen_state[1], left_pov_frozen_state[0], right_pov_frozen_state[1], right_pov_frozen_state[0]);
        }

        return cfg_->traj.integrate_maxt;
    }

    // THIS IS CALCULATE WITH LEFT AND RIGHT VECTORS FROM THE ROBOT'S POV
    double GapFeasibilityChecker::getLeftToRightAngle(Eigen::Vector2d left_norm_vect, Eigen::Vector2d right_norm_vect) {
        double determinant = left_norm_vect[1]*right_norm_vect[0] - left_norm_vect[0]*right_norm_vect[1];
        double dot_product = left_norm_vect[0]*right_norm_vect[0] + left_norm_vect[1]*right_norm_vect[1];

        double left_to_right_angle = std::atan2(determinant, dot_product);
        
        if (left_to_right_angle < 0) {
            left_to_right_angle += 2*M_PI; 
        }

        return left_to_right_angle;
    }

    double GapFeasibilityChecker::atanThetaWrap(double theta) {
        double new_theta = theta;
        while (new_theta <= -M_PI) {
            new_theta += 2*M_PI;
            ROS_INFO_STREAM("wrapping theta: " << theta << " to new_theta: " << new_theta);
        } 
        
        while (new_theta >= M_PI) {
            new_theta -= 2*M_PI;
            ROS_INFO_STREAM("wrapping theta: " << theta << " to new_theta: " << new_theta);
        }

        return new_theta;
    }

    void GapFeasibilityChecker::generateTerminalPoints(dynamic_gap::Gap & gap, double terminal_beta_left, double terminal_reciprocal_range_left, 
                                                                                 double terminal_beta_right, double terminal_reciprocal_range_right) {
        auto egocircle = *msg.get();        
        
        double wrapped_term_beta_left = atanThetaWrap(terminal_beta_left);
        float init_left_idx = (terminal_beta_left - egocircle.angle_min) / egocircle.angle_increment;
        int left_idx = (int) std::floor(init_left_idx);
        float left_dist = (1.0 / terminal_reciprocal_range_left);

        double wrapped_term_beta_right = atanThetaWrap(terminal_beta_right);
        float init_right_idx = (terminal_beta_right - egocircle.angle_min) / egocircle.angle_increment;
        int right_idx = (int) std::floor(init_right_idx);
        float right_dist = (1.0 / terminal_reciprocal_range_right);
        // if (left_idx == right_idx) right_idx++;

        gap.setTerminalPoints(right_idx, right_dist, left_idx, left_dist);
    }

}
