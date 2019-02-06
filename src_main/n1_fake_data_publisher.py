#!/usr/bin/env python
# -*- coding: utf-8 -*-

# -------------------------------------------------------------
# ------------------ Includes
# -- Standard
import open3d
import numpy as np
import sys, os, cv2
from copy import copy
PYTHON_FILE_PATH = os.path.join(os.path.dirname(__file__))+"/"

# -- ROS
import rospy
from tf.transformations import euler_from_quaternion, quaternion_from_euler, euler_matrix

# -- My lib
sys.path.append(PYTHON_FILE_PATH + "../src_python")
from lib_cloud_conversion_between_Open3D_and_ROS import convertCloudFromOpen3dToRos
from lib_geo_trans import transXYZ

# -- Message types
# from std_msgs.msg import Int32  # used for indexing the ith robot pose
from sensor_msgs.msg import PointCloud2  # for DEBUG_MODE_FOR_BAXTER
from scan3d_by_baxter.msg import T4x4


# -------------------------------------------------------------
# ------------------ Variables 
FILE_FOLDER = PYTHON_FILE_PATH+"../data/data/driller_1/" # 10 goals
# FILE_FOLDER = PYTHON_FILE_PATH+"../data/data/bottle_1/" # 11 goals


# -------------------------------------------------------------
# ------------------ Functions 


def getCloudSize(open3d_cloud):
    return np.asarray(open3d_cloud.points).shape[0]


class ClassPubDepthCamPose(object):
    def __init__(self):

        # File to read in pose
        self.openFile()

        # Topic to publish pose
        topic_endeffector_pos = "my/robot_end_effector_pose"
        self.pub = rospy.Publisher(topic_endeffector_pos, T4x4, queue_size=10)

    def openFile(self):
        file_name_camera_pose = "camera_pose.txt"
        self.fin = open(FILE_FOLDER+file_name_camera_pose, "r")

    def closeFile(self):
        self.fin.close()

    def readNextPose(self):
        s_blank = self.fin.readline()
        s_ithpose = self.fin.readline()
        T=np.identity(4)
        for i in range(4):
            s_vals = self.fin.readline()
            vals = [float(s) for s in s_vals.split()]
            for j in range(4):
                T[i][j]=vals[j]
        return T

    def pub_pose(self):
        T = self.readNextPose()

        # Manual offset
        T_rgb_to_depth = transXYZ(x=0, y=-0.025, z=0.1) 
        T = T.dot(T_rgb_to_depth)

        # Trans to 1x16 array
        pose_1x16 = []
        for i in range(4):
            for j in range(4):
                pose_1x16 += [T[i, j]]
        self.pub.publish(pose_1x16)
        return

class ClassPubPointClous(object):
    def __init__(self):

        # File to read in cloud
        file_name_cloud_src = "src_"
        self.getFileName = lambda: FILE_FOLDER + file_name_cloud_src+\
            "{:02d}".format(ith_goalpose)+".pcd"
        
        # Topic to publish cloud
        topic_name_rgbd_cloud = "/camera/depth_registered/points"
        self.pub = rospy.Publisher(
            topic_name_rgbd_cloud, PointCloud2, queue_size=10)

    def pub_cloud(self):
        filename = self.getFileName()
        open3d_cloud = open3d.read_point_cloud(filename)
        print "Node 1: sim: load cloud file:\n  " + filename
        print "  points = " + str(getCloudSize(open3d_cloud))
        
        ros_cloud = convertCloudFromOpen3dToRos(open3d_cloud)
        self.pub.publish(ros_cloud)
        print("Node 1: sim: publishing cloud "+str(ith_goalpose))


# -- Main
if __name__ == "__main__":
    rospy.init_node('node1_fake')

    # -- Publish poses
    pub_poses = ClassPubDepthCamPose()
    
    # # -- Publishes clouds
    pub_clouds = ClassPubPointClous()

    # Move Baxter to all goal positions
    num_goalposes = rospy.get_param("num_goalposes")
    ith_goalpose = 0
    rospy.sleep(3.0)
    while ith_goalpose < num_goalposes and not rospy.is_shutdown():
        ith_goalpose += 1
        print "\n----------------------------------------"
        print "Node 1: Moving to ", ith_goalpose,"th pose:"
        pub_poses.pub_pose()
        pub_clouds.pub_cloud()
        rospy.sleep(1.0)
        if ith_goalpose == num_goalposes:
            pub_poses.closeFile()
            break
            # pub_poses.openFile()
            # ith_goalpose = 0

    pub_poses.closeFile()
    rospy.spin()
    print("!!!!! Node 1 stops.")
