#include <led_detector/LedDetector.h>
#include <ros/ros.h>
using namespace cv;
using namespace std;
using namespace src_perception;

namespace src_qual1_task
{
    LedDetector::LedDetector(ros::NodeHandle nh)
    {
        m_multisenseImagePtr = new src_perception::MultisenseImage(nh);
        // m_multisensePcPtr = NULL;
        m_randomGen= cv::RNG(12345);
        m_imageRGBXYZpub = nh.advertise<led_detector::LedPositionColor>("/detect/light/rgbxyz", 1);
    }
    
    LedDetector::~LedDetector()
    {
    }

    bool getLight(Mat &new_image,geometry_msgs::Point &pixelCoordinates,std::vector<std::vector<cv::Point> > m_gradientContours,cv::RNG m_randomGen)
    {
        bool lightXYDetected = false;
        cv::vector<cv::Mat> inMsgChannel(3),hsv(3);
        cv::Mat inThresh,hist,backProject,drawing;
        int histSize[] = {180,256,256};
        float hRange[] ={0,180};        //hue range
        float sRange[] ={0,256};        //saturation range
        float vRange[] ={0,256};        //values range
        int channels[]={0,1,2};
        const float* histRange[] = { hRange, sRange,vRange};
        cv::vector<cv::Vec4i> hierarchy;
        cv::split(new_image,inMsgChannel);
        
        // RBG channel threshold, 180 is working,lower values gives some noise
        for(int iter=0;iter<3;iter++)
            cv::threshold(inMsgChannel[iter],inMsgChannel[iter],180,255,0);
        cv::merge(inMsgChannel,inThresh);
        cv::cvtColor(inThresh,inThresh,CV_BGR2HSV);
        cv::calcHist(&inThresh,1,channels,cv::Mat(),hist,2,histSize,histRange,true,false);
        cv::split(hist,hsv);
        for(int iter=0;iter<3;iter++)
            cv::normalize(hsv[iter],hsv[iter],0,hsv[iter].rows,cv::NORM_MINMAX,-1,cv::Mat());
        cv::merge(hsv,hist);
        cv::calcBackProject(&inThresh,1,channels,hist,backProject,histRange,1,true);
        cv::bitwise_not(backProject,backProject);
        drawing = backProject.clone();
        cv::threshold(drawing,drawing,180,255,0);

        // Find the contours of the LED
        if (countNonZero(drawing) > 1)
        {
            cv::Mat contourOutput = drawing.clone();
            cv::findContours(contourOutput, m_gradientContours, CV_RETR_LIST, CV_CHAIN_APPROX_NONE);
        }

        std::vector<std::vector<cv::Point> >contours_poly(m_gradientContours.size());       // For polygonal contour
        std::vector<cv::Point2f>points;

        // Approximate the contours to a polygonal shape
        for( int i = 0; i< m_gradientContours.size(); i++ )
        {
            approxPolyDP( cv::Mat(m_gradientContours[i]), contours_poly[i], 3, true );
        }

        // Draws the contours onto the original image. Comment this section if no need to draw contours
        for( int i = 0; i< m_gradientContours.size(); i++ )
        {
            cv::Scalar color = cv::Scalar( m_randomGen.uniform(0, 255), m_randomGen.uniform(0,255), m_randomGen.uniform(0,255) );
            drawContours(new_image, contours_poly, i, color, 1, 8, std::vector<cv::Vec4i>(), 0, cv::Point());
        }

        for (int i = 0, j = 0; j < m_gradientContours.size(); j++)
        {
            cv::Moments moment = cv::moments((cv::Mat)m_gradientContours[j]);
           
            if (moment.m00)
            {
                // Finding x,y coordinates of centroid of the contour
                points.push_back(cv::Point2f(moment.m10/moment.m00,moment.m01/moment.m00));
                cv::Vec3b pixel_value =  new_image.at<cv::Vec3b>(points[i].y,points[i].x);
                cout<<"GetLight"<<endl;

                // Assigning x,y xo-ordinates in image frame
                pixelCoordinates.x = points[i].x;
                pixelCoordinates.y = points[i].y;
                cout<<pixelCoordinates.x<<"    *    "<<pixelCoordinates.y<<endl;
                lightXYDetected = true;
                i++;
            }
        }

        // Comment if no need to display raw image
         cv::imshow("Raw Image with Contours", new_image);
        
        // cv::waitKey(3);//
        return lightXYDetected;
    }


    bool getPoseRGB(ImageFrame &img_frame,geometry_msgs::Point &pixelCoordinates,src_perception::MultisenseImage *m_multisenseImagePtr,led_detector::LedPositionColor message, ros::Publisher m_imageRGBXYZpub)
    {
        bool poseXYZDetected = false;
        tf::TransformListener listener;
        src_perception::StereoPointCloudColor::Ptr organized_cloud(new src_perception::StereoPointCloudColor);
        static tf::TransformBroadcaster br;
        tf::Transform transform;
        tf::Quaternion q;
        pcl::PointXYZRGB p;
        if (m_multisenseImagePtr == NULL)   
            return false;
        try
        {
            listener.waitForTransform("/world", "/left_camera_optical_frame", ros::Time(0), ros::Duration(3.0));
        }
        catch (tf::TransformException ex)
        {
            ROS_ERROR("%s",ex.what());
            ros::spinOnce();
            return false;
        }

        m_multisenseImagePtr->giveDisparityImage(img_frame.m_disparityImage);
        if (img_frame.m_qMatrix.empty())
        {
            if (!m_multisenseImagePtr->giveQMatrix(img_frame.m_qMatrix))
                return false;
        }
        else{
            m_multisenseImagePtr->giveQMatrix(img_frame.m_qMatrix);
        }
      
        // Obtaining a stereo point cloud for Z position and RGB values
        src_perception::PointCloudHelper::generateOrganizedRGBDCloud(img_frame.m_disparityImage,img_frame.m_originalImage,img_frame.m_qMatrix,organized_cloud);
        p = organized_cloud->at(pixelCoordinates.x, pixelCoordinates.y);
        transform.setOrigin( tf::Vector3(p.x, p.y, p.z) );
        q.setRPY(0, 0, 0);
        transform.setRotation(q);
        br.sendTransform(tf::StampedTransform(transform, ros::Time::now(), "left_camera_optical_frame", "LED_frame")); //Co-ordinates wrt left_camera_optical_frame
        
        // Assign XYZ values to ROS message to be published
        message.position.x = p.x;
        message.position.y = p.y;
        message.position.z = p.z;

        // Assign RGB values to ROS message to be published. Getting it from the cloud and not the image
        message.color.r = p.r;
        message.color.g = p.g;
        message.color.b = p.b;
        message.color.a = 1.0;

        poseXYZDetected = true;

        // Publishing XYZ and RGB data
        m_imageRGBXYZpub.publish(message);

        return poseXYZDetected;
    }

    bool LedDetector::detectLight(){
        ImageFrame              img_frame;
        geometry_msgs::Point pixelCoordinates;

        while(ros::ok())
        {
            ros::spinOnce();
            if(m_multisenseImagePtr->giveImage(img_frame.m_originalImage))
            {
                getLight(img_frame.m_originalImage, pixelCoordinates, m_gradientContours,m_randomGen);
                // if(getLight(img_frame.m_originalImage, pixelCoordinates, m_gradientContours,m_randomGen)){
                    if(getPoseRGB(img_frame,pixelCoordinates,m_multisenseImagePtr,message,m_imageRGBXYZpub)){
                        cout<<"Detected"<<endl;
                    }
                    else{
                        cout<<"Not detected"<<endl;   
                    }
                // }
                
            }

        }
    }
}


int main(int argc, char *argv[])
{
    ros::init(argc, argv, "led_detection");
    ros::NodeHandle nh;

    src_qual1_task::LedDetector   led_detect(nh);

    led_detect.m_multisenseImagePtr = new src_perception::MultisenseImage(nh);
    //led_detect.m_multisensePcPtr = new src_perception::MultisensePointCloud(nh,led_detect.m_baseFrame,led_detect.m_leftCameraOpticalFrame);

    while (ros::ok())
    {
        led_detect.detectLight();//tbw
    }
    return 0;
}
