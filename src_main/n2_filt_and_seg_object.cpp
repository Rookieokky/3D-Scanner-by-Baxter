/*
Main function:
* subscribe to cloud_src, filter it, rotated, pub to rviz.
* seg plane, do clustering, pub the object to node3
*/

#include <iostream>
#include <string>
#include <stdio.h>
#include <vector>

#include <ros/ros.h>
#include <pcl_conversions/pcl_conversions.h>

#include <sensor_msgs/PointCloud2.h>
#include "geometry_msgs/Pose.h"

#include "my_basics/basics.h"
#include "my_pcl/pcl_visualization.h"
#include "my_pcl/pcl_commons.h"
#include "my_pcl/pcl_filters.h"
#include "my_pcl/pcl_advanced.h"
#include "my_pcl/pcl_io.h"
#include "scan3d_by_baxter/T4x4.h" // my message

using namespace std;
using namespace pcl;

// -- Params

// Filenames for writing to file
string file_folder, file_name_cloud_src, file_name_cloud_segmented; // filenames for writing cloud to file
int file_name_index_width;

// -- Vars

// Vars for workflow control
bool flag_receive_from_node1 = false;
bool flag_receive_kinect_cloud = false;

// Data contents
float T_baxter_to_depthcam[4][4];
PointCloud<PointXYZRGB>::Ptr cloud_src(new PointCloud<PointXYZRGB>);
PointCloud<PointXYZRGB>::Ptr cloud_rotated(new PointCloud<PointXYZRGB>);   // this pubs to rviz
PointCloud<PointXYZRGB>::Ptr cloud_segmented(new PointCloud<PointXYZRGB>); // this pubs to node3

// -- Input/Output and Sub/Publisher
template <typename T>
T getParam(const string key)
{
    static ros::NodeHandle nh;
    static T val;
    bool res = nh.getParam(key, val);
    if (!res)
    {
        cout << "my Error in reading ROS param: " << key << endl;
        assert(0);
    }
    return val;
}

void read_T_from_file(float T_16x1[16], string filename);
void callbackFromNode1(const scan3d_by_baxter::T4x4::ConstPtr &pose_message);
void callbackFromKinect(const sensor_msgs::PointCloud2 &ros_cloud);
void pubPclCloudToTopic(ros::Publisher &pub, PointCloud<PointXYZRGB>::Ptr pcl_cloud);

// -- Main processing functions
void process_to_get_cloud_rotated();
void process_to_get_cloud_segmented();
void print_cloud_processing_result(int cnt_cloud);

// -- Main Loop:
void main_loop(ros::Publisher &pub_to_node3, ros::Publisher &pub_to_rviz)
{
    int cnt_cloud = 0;
    while (ros::ok())
    {
        if (flag_receive_kinect_cloud)
        {
            flag_receive_kinect_cloud = false;
            cnt_cloud++;

            // Process cloud
            process_to_get_cloud_rotated();
            pubPclCloudToTopic(pub_to_rviz, cloud_rotated);

            process_to_get_cloud_segmented();
            pubPclCloudToTopic(pub_to_node3, cloud_segmented);

            // Save to file
            string suffix = my_basics::int2str(cnt_cloud, file_name_index_width) + ".pcd";

            string f0 = file_folder + file_name_cloud_src + suffix;
            my_pcl::write_point_cloud(f0, cloud_src);

            string f2 = file_folder + file_name_cloud_segmented + suffix;
            my_pcl::write_point_cloud(f2, cloud_segmented);

            // print
            print_cloud_processing_result(cnt_cloud); // Print info
        }
        ros::spinOnce(); // In python, sub is running in different thread. In C++, same thread. So need this.
        ros::Duration(0.01).sleep();
    }
}

// -- Main (Only for setting up variables. The main loop is at above.)
int main(int argc, char **argv)
{
    // Init node
    string node_name = "node2";
    ros::init(argc, argv, node_name);
    ros::NodeHandle nh;

    // Settings: topic names
    string topic_n1_to_n2, topic_n2_to_n3, topic_name_rgbd_cloud, topic_n2_to_rviz;
    topic_n1_to_n2 = getParam<string>("topic_n1_to_n2");
    topic_n2_to_n3 = getParam<string>("topic_n2_to_n3");
    topic_name_rgbd_cloud = getParam<string>("topic_name_rgbd_cloud");
    topic_n2_to_rviz = getParam<string>("topic_n2_to_rviz");

    // Settings: file names for saving point cloud
    if (!nh.getParam("file_folder", file_folder))
        assert(0);
    if (!nh.getParam("file_name_cloud_src", file_name_cloud_src))
        assert(0);
    if (!nh.getParam("file_name_cloud_segmented", file_name_cloud_segmented))
        assert(0);
    if (!nh.getParam("file_name_index_width", file_name_index_width))
        assert(0);

    // Subscriber and Publisher
    ros::Subscriber sub_from_node1 = nh.subscribe(topic_n1_to_n2, 1, callbackFromNode1); // 1 is queue size
    ros::Subscriber sub_from_kinect = nh.subscribe(topic_name_rgbd_cloud, 1, callbackFromKinect);
    ros::Publisher pub_to_node3 = nh.advertise<sensor_msgs::PointCloud2>(topic_n2_to_n3, 1);
    ros::Publisher pub_to_rviz = nh.advertise<sensor_msgs::PointCloud2>(topic_n2_to_rviz, 1);

    // -- Loop, subscribe ros_cloud, and view
    main_loop(pub_to_node3, pub_to_rviz);

    // Return
    ROS_INFO("Node2 stops");
    return 0;
}

// ================================================================================
// =========================== Cloud Processing====================================
// ================================================================================

// -----------------------------------------------------
// -----------------------------------------------------
void process_to_get_cloud_rotated()
{
    // Func: Filtering, rotate cloud to Baxter robot frame

    // voxel filtering
    static float x_grid_size = 0.005, y_grid_size = 0.005, z_grid_size = 0.005;

    // read params from ros for filtering settings
    static int cnt_called_times = 0;
    if (cnt_called_times++ == 0)
    {
        ros::NodeHandle nh("~");

        // -- filtByVoxelGrid
        if (!nh.getParam("x_grid_size", x_grid_size))
            assert(0);
        if (!nh.getParam("y_grid_size", y_grid_size))
            assert(0);
        if (!nh.getParam("z_grid_size", z_grid_size))
            assert(0);
    }

    // -- filtByVoxelGrid
    pcl::copyPointCloud(*cloud_src, *cloud_rotated);
    cloud_rotated = my_pcl::filtByVoxelGrid(cloud_rotated, x_grid_size, y_grid_size, z_grid_size);

    // -- filtByStatisticalOutlierRemoval
    float mean_k = 50, std_dev = 1.0;
    cloud_rotated = my_pcl::filtByStatisticalOutlierRemoval(cloud_rotated, mean_k, std_dev);

    // -- rotate cloud to Baxter's frame
    for (PointXYZRGB &p : cloud_rotated->points)
        my_basics::preTranslatePoint(T_baxter_to_depthcam, p.x, p.y, p.z);
}

// -----------------------------------------------------
// -----------------------------------------------------
void process_to_get_cloud_segmented()
{
    // Func: Range filtering，　Remove plane(table), do clustering, choose the largest one

    // Range filtering
    static bool flag_do_range_filt;
    static float x_range_radius, y_range_radius, z_range_low, z_range_up;
    static float chessboard_x = 0.0, chessboard_y = 0.0, chessboard_z = 0.0; // Should read from txt
    static float T_baxter_to_chess[4][4] = {0};

    // plane segmentation
    static float plane_distance_threshold = 0.01;
    static int plane_max_iterations = 100;
    static int num_planes = 1;
    static float ratio_of_rest_points = -1; // disabled

    // divide cloud into clusters
    static bool flag_do_clustering = true;
    static double cluster_tolerance = 0.02;
    static int min_cluster_size = 100, max_cluster_size = 10000;

    // read params from ros
    static int cnt_called_times = 0;
    if (cnt_called_times++ == 0)
    {
        ros::NodeHandle nh("~");
        ros::NodeHandle nhg("");

        // -- filtByPassThrough
        if (!nh.getParam("flag_do_range_filt", flag_do_range_filt))
            assert(0);
        if (!nh.getParam("x_range_radius", x_range_radius))
            assert(0);
        if (!nh.getParam("y_range_radius", y_range_radius))
            assert(0);
        if (!nh.getParam("z_range_low", z_range_low))
            assert(0);
        if (!nh.getParam("z_range_up", z_range_up))
            assert(0);

        // Read chessboard's pose
        string file_folder_config, file_name_T_baxter_to_chess;
        if (!nhg.getParam("file_folder_config", file_folder_config))
            assert(0);
        if (!nhg.getParam("file_name_T_baxter_to_chess", file_name_T_baxter_to_chess))
            assert(0);
        float tmpT[16] = {0};
        read_T_from_file(tmpT, file_folder_config + file_name_T_baxter_to_chess);
        for (int cnt = 0, i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                T_baxter_to_chess[i][j] = tmpT[cnt++];
        chessboard_x = T_baxter_to_chess[0][3];
        chessboard_y = T_baxter_to_chess[1][3];
        chessboard_z = T_baxter_to_chess[2][3];

        // Segment plane
        if (!nh.getParam("plane_distance_threshold", plane_distance_threshold))
            assert(0);
        if (!nh.getParam("plane_max_iterations", plane_max_iterations))
            assert(0);
        if (!nh.getParam("num_planes", num_planes))
            assert(0);

        // Clustering
        if (!nh.getParam("flag_do_clustering", flag_do_clustering))
            assert(0);
        if (!nh.getParam("cluster_tolerance", cluster_tolerance))
            assert(0);
        if (!nh.getParam("min_cluster_size", min_cluster_size))
            assert(0);
        if (!nh.getParam("max_cluster_size", max_cluster_size))
            assert(0);
    }

    // -- filtByPassThrough (by range)
    // PointCloud<PointXYZRGB>::Ptr tmp_cloud(new PointCloud<PointXYZRGB>);
    copyPointCloud(*cloud_rotated, *cloud_segmented);
    if (flag_do_range_filt)
    {
        // cout<<"Debug: before range filter"<<endl;
        // my_pcl::printCloudSize(cloud_segmented);
        cloud_segmented = my_pcl::filtByPassThrough(
            cloud_segmented, "x", chessboard_x + x_range_radius, chessboard_x - x_range_radius);

        cloud_segmented = my_pcl::filtByPassThrough(
            cloud_segmented, "y", chessboard_y + y_range_radius, chessboard_y - y_range_radius);

        cloud_segmented = my_pcl::filtByPassThrough(
            cloud_segmented, "z", chessboard_z + z_range_up, chessboard_z + z_range_low);
        // cout<<"Debug: after range filter"<<endl;
        // my_pcl::printCloudSize(cloud_segmented);
    }

    // -- Remove planes
    int num_removed_planes = my_pcl::removePlanes(
        cloud_segmented,
        plane_distance_threshold, plane_max_iterations,
        num_planes, ratio_of_rest_points);

    // -- Clustering: Divide the remaining point cloud into different clusters
    if (flag_do_clustering)
    {
        vector<PointIndices> clusters_indices = my_pcl::divideIntoClusters(
            cloud_segmented, cluster_tolerance, min_cluster_size, max_cluster_size);

        // -- Extract indices into cloud clusters
        vector<PointCloud<PointXYZRGB>::Ptr> cloud_clusters =
            my_pcl::extractSubCloudsByIndices(cloud_segmented, clusters_indices);
        cloud_segmented = cloud_clusters[0];
    }

    // -- rotate cloud to Chessboard's frame, for better viewing in PCL viewer
    for (PointXYZRGB &p : cloud_segmented->points)
        my_basics::preTranslatePoint(T_baxter_to_chess, p.x, p.y, p.z);
}

// -----------------------------------------------------
// -----------------------------------------------------
void print_cloud_processing_result(int cnt_cloud)
{

    cout << endl;
    printf("------------------------------------------\n");
    printf("-------- Processing %dth cloud -----------\n", cnt_cloud);
    ROS_INFO("Subscribed a point cloud from ros topic.");

    cout << "camera pos:" << endl;
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
            cout << T_baxter_to_depthcam[i][j] << " ";
        cout << endl;
    }
    cout << endl;

    cout << "cloud_src: ";
    my_pcl::printCloudSize(cloud_src);

    cout << "cloud_rotated: ";
    my_pcl::printCloudSize(cloud_rotated);

    cout << "cloud_segmented: ";
    my_pcl::printCloudSize(cloud_segmented);
    printf("------------------------------------------\n\n");
}

void read_T_from_file(float T_16x1[16], string filename)
{
    ifstream fin;
    fin.open(filename);
    float val;
    int cnt = 0;
    assert(fin.is_open()); // Fail to find the config file
    while (fin >> val)
        T_16x1[cnt++] = val;
    fin.close();
    return;
}

void callbackFromNode1(const scan3d_by_baxter::T4x4::ConstPtr &pose_message)
{
    flag_receive_from_node1 = true;
    const vector<float> &trans_mat_16x1 = pose_message->TransformationMatrix;
    for (int cnt = 0, i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            T_baxter_to_depthcam[i][j] = trans_mat_16x1[cnt++];
}
void callbackFromKinect(const sensor_msgs::PointCloud2 &ros_cloud)
{
    if (flag_receive_from_node1)
    {
        flag_receive_from_node1 = false;
        flag_receive_kinect_cloud = true;
        fromROSMsg(ros_cloud, *cloud_src);
    }
}
void pubPclCloudToTopic(ros::Publisher &pub, PointCloud<PointXYZRGB>::Ptr pcl_cloud)
{
    sensor_msgs::PointCloud2 ros_cloud_to_pub;
    pcl::toROSMsg(*pcl_cloud, ros_cloud_to_pub);
    ros_cloud_to_pub.header.frame_id = "base";
    pub.publish(ros_cloud_to_pub);
}