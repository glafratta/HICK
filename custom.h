#include "configurator.h"
#include "libcam2opencv.h"
#include "a1lidarrpi.h"
#include "alphabot.h"
#include "Iir.h"
#include "CppTimer.h"
#include <stdio.h>
#include <stdlib.h>
#include <bits/stdc++.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#define _USE_MATH_DEFINES

std::vector <BodyFeatures> WorldBuilder::processData(CoordinateContainer points){
    int count =0;
    std::vector <BodyFeatures> result;
    for (Pointf p: points){
        if (count%2==0){
            BodyFeatures feature;
            feature.pose.p = getb2Vec2(p); 
            result.push_back(feature);  
        }
        count++;
    }
    return result;
}


class LidarInterface : public A1Lidar::DataInterface{
ConfiguratorInterface * ci;
public: 
    int mapCount =0;

    LidarInterface(ConfiguratorInterface * _ci): ci(_ci){}

	void newScanAvail(float, A1LidarData (&data)[A1Lidar::nDistance]){ //uncomment sections to write x and y to files
		if (ci == NULL){
			printf("null pointer to ci\n");
			return;
		}
		ci->data.clear();
		ci->data2fp.clear();
		mapCount++;
		Pointf p, p2f;
		FILE *f;
		char name[256];
		sprintf(name,"/tmp/map%04i.dat", mapCount);
		printf("%s\n", name);
		if (ci->debugOn){
			f=fopen(name, "w");
		}
		for (A1LidarData &data:data){
			if (data.valid&& data.r <LIDAR_RANGE){
				//DATA IS ROUNDED AND DUPLICATES ARE ELIMINATED
				float x = round(data.x*1000)/1000;
				float y = round(data.y*1000)/1000;
				float x2 = round(data.x*100)/100;
				float y2 = round(data.y*100)/100;
				p= (Pointf(x, y));
				p2f=Pointf(x2, y2);
				ci->data.insert(p);
				ci->data2fp.insert(p2f);
				if (ci->debugOn){
					fprintf(f, "%.2f\t%.2f\n", p2f.x, p2f.y);
				}
            }
		}
		if (ci->debugOn){
		fclose(f);
		}
		if (!ci->data.empty()){
			ci->setReady(1);
		}
		ci->iteration++;

	}


};

class MotorCallback :public AlphaBot::StepCallback { //every 100ms the callback updates the plan
    float L=0;
	float R=0;
	vertexDescriptor start=TransitionSystem::null_vertex();
public:
int ogStep=0;
Configurator * c;

MotorCallback(Configurator *conf): c(conf){
	start=c->currentVertex;
}
void step( AlphaBot &motors){
	c->printPlan();
	printf("graph size=%i\n", c->transitionSystem.m_vertices.size());
	if (c->getIteration() <=0){
		return;
	}
	//if (c->planVertices.empty() & c->planning){
	if (!c->running){
		motors.setRightWheelSpeed(0);
 	   motors.setLeftWheelSpeed(0);		
	}
    ExecutionError ee =c->trackTaskExecution(*c->getTask());
    if (c->getTask()->motorStep>0){
        c->getTask()->correct(c->getTask()->action, c->getTask()->motorStep);
    }
	EndedResult er = c->controlGoal.checkEnded();
	printf("control goal start: %f, %f, %f\n", c->controlGoal.start.p.x, c->controlGoal.start.p.y, c->controlGoal.start.q.GetAngle());
	printf("start vertex v=%i, position= %f, %f\n", start, c->transitionSystem[start].endPose.p.x,  c->transitionSystem[start].endPose.p.y);
	if (er.ended){
		Disturbance new_goal(PURSUE, c->transitionSystem[start].endPose.p);
		printf("new goal v=%i, position= %f, %f, valid =%i\n", start, new_goal.pose().p.x, new_goal.pose().p.y, new_goal.isValid());
		c->controlGoal = Task(new_goal, DEFAULT);
		printf("goal reached\n");
	}
	if (er.ended || start==c->movingVertex){
		start=c->currentVertex;
	}
	c->planVertices = c->changeTask(c->getTask()->change,  ogStep, c->planVertices);
    motors.setRightWheelSpeed(c->getTask()->getAction().getRWheelSpeed()); //temporary fix because motors on despacito are the wrong way around
    motors.setLeftWheelSpeed(c->getTask()->getAction().getLWheelSpeed());
	printf("og step: %i ,R=%f\tL=%f, vertex=%i\n", ogStep, c->getTask()->getAction().getRWheelSpeed(), c->getTask()->getAction().getLWheelSpeed(), c->currentVertex);
}
};

struct CameraCallback: Libcam2OpenCV::Callback {
    char dumpname[50];
    double signal=0;
    double filtered_signal=0;
    Iir::Butterworth::LowPass<order>low_pass;
    Iir::Butterworth::BandStop<order>band_stop;
	const int reset_hz=10;

    CameraCallback(MotorCallback * _cb):cb(_cb){
        low_pass.setup(FPS, cutoff_frequency);
        band_stop.setup(FPS, DC, band_width);
    }


	void hasFrame(const cv::Mat &frame, const libcamera::ControlList &){
		if (cb==NULL){
			printf("null cb\n");
		}
		if (cb->c==NULL){
			printf("null c\n");
		}
		if (cb->c->getTask()==NULL){
			printf("null task\n");
		}
		//cv::Mat frame_cropped=frame(cv::Range(0, frame.width()), cv::Range(f	rame.height()*2/3, frame.height()));
        float error=0;
	//	if (cb->c->getTask()->motorStep%reset_hz==0){
			//imgProc.reset();
			//cb->c->getTask()->correct.reset();
	//	}
        cv::Vec2d  optic_flow=imgProc.avgOpticFlow(frame);
        cv::Vec2d  optic_flow_filtered=optic_flow;
        signal= signal+optic_flow[0];
        optic_flow_filtered[0]=low_pass.filter((optic_flow[0]));
        optic_flow_filtered[0]= band_stop.filter(optic_flow_filtered[0]);
        filtered_signal=filtered_signal+optic_flow_filtered[0];
		if (cb->c->getTask()->motorStep!=cb->ogStep & cb->c->getTask()->motorStep!=0){ //, in the future t.motorStepdiscard will be t.change
																//signal while the robot isn' moving
        	Task::Action action= cb->c->getTask()->getAction();
			error= cb->c->getTask()->correct.errorCalc(action, double(optic_flow_filtered[0]));
		}
        cb->c->getTask()->correct.update(error); //for now just going straight
    }
private:
ImgProc imgProc;
MotorCallback *cb=NULL;
};

float Configurator::taskRotationError(){
    return 0;
}