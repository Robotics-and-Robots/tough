#include <val_control_common/val_control_common.h>

valControlCommon::valControlCommon(ros::NodeHandle nh): nh_(nh), armTraj(nh), chestTraj(nh), pelvisTraj(nh)

{
    // set the publisher
    stop_traj_pub_ = nh_.advertise<ihmc_msgs::StopAllTrajectoryRosMessage>("/ihmc_ros/valkyrie/control/stop_all_trajectories",1,true);
}

valControlCommon::~valControlCommon()
{

}

void valControlCommon::stopAllTrajectories(void)
{
    ihmc_msgs::StopAllTrajectoryRosMessage stop_msg;
    stop_msg.unique_id = -1;

    // send the message
    stop_traj_pub_.publish(stop_msg);

    ros::Duration(1).sleep();
}

void valControlCommon::resetRobot()
{
    armTraj.moveToZeroPose(armSide::LEFT);
    ros::Duration(0.3).sleep();
    armTraj.moveToZeroPose(armSide::RIGHT);
    ros::Duration(1.5).sleep();

    pelvisTraj.controlPelvisHeight(0.9);
    ros::Duration(1.5).sleep();
    chestTraj.controlChest(2,2,2);
    ros::Duration(1).sleep();

    armTraj.moveToDefaultPose(armSide::LEFT);
    ros::Duration(0.3).sleep();
    armTraj.moveToDefaultPose(armSide::RIGHT);
    ros::Duration(1.5).sleep();
}
