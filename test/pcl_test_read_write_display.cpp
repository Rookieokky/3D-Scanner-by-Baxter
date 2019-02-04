/*
Test functions in "my_pcl/pcl_io.h", "my_pcl/pcl_commons.h", "my_pcl/pcl_visualization.h":
    read, write, display, and change color to the cloud->points.

Example of usage:
$ bin/pcl_test_read_write_display data/data/gray_milk.pcd
$ bin/pcl_test_read_write_display data/data/color_milk_and_2bottles.pcd

*/

#include "my_pcl/pcl_io.h"
#include "my_pcl/pcl_commons.h"
#include "my_pcl/pcl_visualization.h"

using namespace std;
using namespace my_pcl;

typedef pcl::PointXYZRGB PointT;
typedef pcl::PointCloud<PointT> PointCloudT;

int main(int argc, char **argv)
{

    // -- Load point cloud
    string filename = argv[1];
    PointCloudT::Ptr cloud;
    read_point_cloud(filename, cloud);

    // -- Test write point cloud
    string output_folder = "./";
    write_point_cloud(output_folder + "tmp.pcd", cloud);

    // -- Set all colors to red
    unsigned char r = 255, g = 0, b = 1;
    for (auto &point : cloud->points)
        setPointColor(point, r, g, b);

    // -- Init viewer
    string viewer_name="viewer name", cloud_name="cloud name";
    boost::shared_ptr<pcl::visualization::PCLVisualizer>
        viewer = initPointCloudRGBViewer(cloud, viewer_name, cloud_name);

    // -- Display point cloud in a loop
    int cnt=0, direction=1;
    while (1)
    {

        // -- Change color
        if(r>=255)direction=-1;
        if(r<=0)direction=+1;
        r=r+direction;
        for (auto &point : cloud->points)
            setPointColor(point, r, g, b);
        cout <<"color:"<<int(r)<<endl;

        // -- Display
        viewer->spinOnce(40);
        viewer->updatePointCloud(cloud, cloud_name);
        if (viewer->wasStopped())
            break;
        cnt++;
    }
    return (0);
}
