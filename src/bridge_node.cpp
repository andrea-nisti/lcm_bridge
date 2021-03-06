#include "lcm/lcm-cpp.hpp"
#include "ros/ros.h"
#include "geometry_msgs/PoseStamped.h"
#include "geometry_msgs/TwistStamped.h"
#include "mavros_msgs/State.h"
#include "mavros_msgs/ExtendedState.h"
#include "lcm_messages/geometry/pose.hpp"
#include "lcm_messages/exec/state.hpp"
#include "common/MavState.h"
#include "common/CallbackHandler.hpp"
#include "nav_msgs/Odometry.h"
#include <poll.h>

lcm::LCM handler, handler2, handler3;
geometry::pose lcm_pose;
exec::state robot_state;
nav_msgs::Odometry platPos;
mavros_msgs::State disarmed;
mavros_msgs::ExtendedState landed;
CallbackHandler call;

bool firstState = true;
bool firstEState = true;

void odometryCallback(nav_msgs::Odometry pose){

    lcm_pose.position[0] =  pose.pose.pose.position.x;
    lcm_pose.position[1] =  pose.pose.pose.position.y;
    lcm_pose.position[2] =  pose.pose.pose.position.z;

    lcm_pose.velocity[0] = pose.twist.twist.linear.x;
    lcm_pose.velocity[1] = pose.twist.twist.linear.y;
    lcm_pose.velocity[2] = pose.twist.twist.linear.z;

    lcm_pose.orientation[0] = pose.pose.pose.orientation.w;
    lcm_pose.orientation[1] = pose.pose.pose.orientation.x;
    lcm_pose.orientation[2] = pose.pose.pose.orientation.y;
    lcm_pose.orientation[3] = pose.pose.pose.orientation.z;

    handler.publish("vision_position_estimate",&lcm_pose);

}

void stateCallback(mavros_msgs::State s){

    if (firstState) {
        disarmed.armed = s.armed;
        firstState = false;
    }

    //Store arming state
    if(disarmed.armed == s.armed) robot_state.armed = 0;
    else robot_state.armed = 1;

}
void EStateCallback(mavros_msgs::ExtendedState es){

    if (firstEState) {
        landed.landed_state = es.landed_state;
        firstEState = false;
    }

    if(landed.landed_state == es.landed_state) robot_state.landed = 1;
    else robot_state.landed = 0;

}


int main(int argc, char **argv)
{

    //ROS helpers
    ros::init(argc, argv, "ros2lcm_bridge");
    ros::NodeHandle n;
    ros::Subscriber odometry_sub = n.subscribe("/mavros/local_position/odom",1,&odometryCallback);
    ros::Subscriber state_sub = n.subscribe("/mavros/state",1,&stateCallback);
    ros::Subscriber state_extended_sub = n.subscribe("/mavros/extended_state",1,&EStateCallback);

    ros::Publisher  pub  = n.advertise<geometry_msgs::PoseStamped>("/mavros/setpoint_position/local",1);
    ros::Publisher  pub1 = n.advertise<nav_msgs::Odometry>("/platform_position",1);
    ros::Publisher  pub2 = n.advertise<geometry_msgs::TwistStamped>("/mavros/setpoint_velocity/cmd_vel",1);

    //LCM stuff
    lcm::Subscription *sub2 = handler2.subscribe("local_position_sp", &CallbackHandler::positionSetpointCallback, &call);
    lcm::Subscription *sub3 = handler3.subscribe("platRob"    , &CallbackHandler::visionEstimateCallback, &call);
    sub2->setQueueCapacity(1);
    sub3->setQueueCapacity(1);

    struct pollfd fds[2];
    fds[0].fd = handler2.getFileno(); // Robot position
    fds[0].events = POLLIN;

    fds[1].fd = handler3.getFileno(); // Platform position
    fds[1].events = POLLIN;

    robot_state.landed = 1;
    robot_state.armed  = 0;

    //main loop
    ros::Rate loop_rate(30);
    int stateRate = 0;
    int*  platformDataRec = new int(0);
    int*  robotDataRec    = new int(0);

    while (ros::ok()){

        //Poll file descriptors
        int ret = poll(fds,2,0);
        *platformDataRec = fds[1].revents & POLLIN;
        *robotDataRec    = fds[0].revents & POLLIN;

        //Platform position POLLIN
        if(*platformDataRec){

            handler3.handle();
            platPos.pose.pose.position.x = call._vision_pos.getX();
            platPos.pose.pose.position.y = call._vision_pos.getY();
            platPos.pose.pose.position.z = call._vision_pos.getZ();

            platPos.twist.twist.linear.x = call._vision_pos.getVx();
            platPos.twist.twist.linear.y = call._vision_pos.getVy();
            platPos.twist.twist.linear.z = call._vision_pos.getVz();

            platPos.header.stamp = ros::Time::now();
            pub1.publish(platPos);

        }

        //Position Command POLLIN
        if(*robotDataRec){

            if(call._position_sp.getType() == MavState::type::POSITION) {
                geometry_msgs::PoseStamped commandPose;
                handler2.handle();

                commandPose.pose.position.x = call._position_sp.getX();
                commandPose.pose.position.y = call._position_sp.getY();
                commandPose.pose.position.z = call._position_sp.getZ();

                commandPose.pose.orientation.x = call._position_sp.getOrientation().x();
                commandPose.pose.orientation.y = call._position_sp.getOrientation().y();
                commandPose.pose.orientation.z = call._position_sp.getOrientation().z();
                commandPose.pose.orientation.w = call._position_sp.getOrientation().w();

                std::cout << "command: " << commandPose.pose.position.x << " " << commandPose.pose.position.y << " " <<commandPose.pose.position.z << std::endl;
                pub.publish(commandPose);
            }
            else if(call._position_sp.getType() == MavState::type::VELOCITY) {
                geometry_msgs::TwistStamped commandPose;
                handler2.handle();

                commandPose.twist.linear.x =  call._position_sp.getVx();
                commandPose.twist.linear.y =  call._position_sp.getVy();
                commandPose.twist.linear.z =  call._position_sp.getVz();

                std::cout << "commandV: " << commandPose.twist.linear.x << " " << commandPose.twist.linear.y << " " <<commandPose.twist.linear.z << std::endl;
                pub2.publish(commandPose);
            }

            ROS_INFO_ONCE("publish ros command");

        }

        //Publish state sometimes
        if (stateRate++ > 10){
            handler2.publish("state",&robot_state);
            stateRate = 0;
        }


        ROS_INFO_ONCE("Spinning");
        ros::spinOnce();
        loop_rate.sleep();

    }

    delete platformDataRec;
    delete robotDataRec;

    return 0;

}
