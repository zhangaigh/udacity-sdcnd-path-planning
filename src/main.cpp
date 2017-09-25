#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"
#include "spline.h"

using namespace std;

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

double distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}
int ClosestWaypoint(double x, double y, const vector<double> &maps_x, const vector<double> &maps_y)
{

	double closestLen = 100000; //large number
	int closestWaypoint = 0;

	for(int i = 0; i < maps_x.size(); i++)
	{
		double map_x = maps_x[i];
		double map_y = maps_y[i];
		double dist = distance(x,y,map_x,map_y);
		if(dist < closestLen)
		{
			closestLen = dist;
			closestWaypoint = i;
		}

	}

	return closestWaypoint;

}

int NextWaypoint(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{

	int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

	double map_x = maps_x[closestWaypoint];
	double map_y = maps_y[closestWaypoint];

	double heading = atan2( (map_y-y),(map_x-x) );

	double angle = abs(theta-heading);

	if(angle > pi()/4)
	{
		closestWaypoint++;
	}

	return closestWaypoint;

}

// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

	int prev_wp;
	prev_wp = next_wp-1;
	if(next_wp == 0)
	{
		prev_wp  = maps_x.size()-1;
	}

	double n_x = maps_x[next_wp]-maps_x[prev_wp];
	double n_y = maps_y[next_wp]-maps_y[prev_wp];
	double x_x = x - maps_x[prev_wp];
	double x_y = y - maps_y[prev_wp];

	// find the projection of x onto n
	double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
	double proj_x = proj_norm*n_x;
	double proj_y = proj_norm*n_y;

	double frenet_d = distance(x_x,x_y,proj_x,proj_y);

	//see if d value is positive or negative by comparing it to a center point

	double center_x = 1000-maps_x[prev_wp];
	double center_y = 2000-maps_y[prev_wp];
	double centerToPos = distance(center_x,center_y,x_x,x_y);
	double centerToRef = distance(center_x,center_y,proj_x,proj_y);

	if(centerToPos <= centerToRef)
	{
		frenet_d *= -1;
	}

	// calculate s value
	double frenet_s = 0;
	for(int i = 0; i < prev_wp; i++)
	{
		frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
	}

	frenet_s += distance(0,0,proj_x,proj_y);

	return {frenet_s,frenet_d};

}

// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, const vector<double> &maps_s, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int prev_wp = -1;

	while(s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1) ))
	{
		prev_wp++;
	}

	int wp2 = (prev_wp+1)%maps_x.size();

	double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),(maps_x[wp2]-maps_x[prev_wp]));
	// the x,y,s along the segment
	double seg_s = (s-maps_s[prev_wp]);

	double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
	double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

	double perp_heading = heading-pi()/2;

	double x = seg_x + d*cos(perp_heading);
	double y = seg_y + d*sin(perp_heading);

	return {x,y};

}

int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  ifstream in_map_(map_file_.c_str(), ifstream::in);

  string line;
  while (getline(in_map_, line)) {
  	istringstream iss(line);
  	double x;
  	double y;
  	float s;
  	float d_x;
  	float d_y;
  	iss >> x;
  	iss >> y;
  	iss >> s;
  	iss >> d_x;
  	iss >> d_y;
  	map_waypoints_x.push_back(x);
  	map_waypoints_y.push_back(y);
  	map_waypoints_s.push_back(s);
  	map_waypoints_dx.push_back(d_x);
  	map_waypoints_dy.push_back(d_y);
  }
  
  unsigned int lane = 1;
  double ref_vel = 49.5;
  double max_vel = 49.5;

  h.onMessage([&map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy, &lane, &ref_vel, &max_vel](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
        	// Main car's localization Data
          	double car_x = j[1]["x"];
          	double car_y = j[1]["y"];
          	double car_s = j[1]["s"];
          	double car_d = j[1]["d"];
          	double car_yaw = j[1]["yaw"];
          	double car_speed = j[1]["speed"];

          	// Previous path data given to the Planner
          	auto previous_path_x = j[1]["previous_path_x"];
          	auto previous_path_y = j[1]["previous_path_y"];
          	// Previous path's end s and d values 
          	double end_path_s = j[1]["end_path_s"];
          	double end_path_d = j[1]["end_path_d"];

          	// Sensor Fusion Data, a list of all other cars on the same side of the road.
          	auto sensor_fusion = j[1]["sensor_fusion"];

          	json msgJson;
            
            int prev_size = previous_path_x.size(); 
            double global_yaw, global_x, global_y;
            const double mile_ph_to_meter_ps = 0.44704; // 1Mph * 1609.344meter/h / 3600 = 0.44704 m/s
            vector<double> ptsx;
          	vector<double> ptsy;
            
            bool too_close = false;
            
            if(prev_size >0){
              car_s = end_path_s;
            }
            
            for(int i=0; i < sensor_fusion.size(); i++){
              // car is in my lane
              float d = sensor_fusion[i][6];
              if(d < (2*4*lane+1.8) && d > (2+4*lane-1.8)){
                double vx = sensor_fusion[i][3];
                double vy = sensor_fusion[i][4];
                double check_speed = sqrt(vx*vx + vy*vy);
                double check_car_s = sensor_fusion[i][5];
                
                check_car_s += (double)prev_size * 0.02 * check_speed;
                
                if((check_car_s > car_s) && (check_car_s - car_s < 30)){
                  too_close = true;
                } 
              }
            }

            // check if there are previous way points that can be used 
            if(prev_size < 2){
              global_x = car_x;
              global_y = car_y;
              global_yaw = deg2rad(car_yaw);
              
              // store the cars previous coordinates and transform them into the cars reference frame
              double shift_x = (global_x - cos(global_yaw)) - global_x;
              double shift_y = (global_y - sin(global_yaw)) - global_y;
              
              ptsx.push_back(shift_x*cos(0-global_yaw) - shift_y*sin(0-global_yaw));
              ptsy.push_back(shift_x*sin(0-global_yaw) + shift_y*cos(0-global_yaw));

              // store the cars current coordinates and transform them into the cars reference frame
              // since the current car coordinates are the origin store (0,0) 
              ptsx.push_back(0);
              ptsy.push_back(0);
            } else {
              global_x = previous_path_x[prev_size - 1];
              global_y = previous_path_y[prev_size - 1];
              double global_x_prev = previous_path_x[prev_size - 2];
              double global_y_prev = previous_path_y[prev_size - 2];
              
              global_yaw = atan2(global_y - global_y_prev, global_x - global_x_prev);
              
              // store the cars previous coordinates and transform them into the cars reference frame
              double shift_x = global_x_prev - global_x;
              double shift_y = global_y_prev - global_y;
              
              ptsx.push_back(shift_x*cos(0-global_yaw) - shift_y*sin(0-global_yaw));
              ptsy.push_back(shift_x*sin(0-global_yaw) + shift_y*cos(0-global_yaw));

              // store the cars current coordinates and transform them into the cars reference frame
              // since the current car coordinates are the origin store (0,0) 
              ptsx.push_back(0);
              ptsy.push_back(0);
            }

            // Frenet coordinates are referenced from the center yellow lines and positive d being on the right.
            double next_d = 2 + 4 * lane; //calculate the cars center d position based on the desired lane
            // get select way points in the future to predict a spline functions for the desired path  
            for(int i=1; i <= 3; i++){
              vector<double> xy_pts = getXY(car_s+(i*30), next_d, map_waypoints_s, map_waypoints_x, map_waypoints_y);
              
              // convert these future points into car centric coordinates
              double shift_x = xy_pts[0] - global_x;
              double shift_y = xy_pts[1] - global_y;
            
              ptsx.push_back(shift_x*cos(0-global_yaw) - shift_y*sin(0-global_yaw));
              ptsy.push_back(shift_x*sin(0-global_yaw) + shift_y*cos(0-global_yaw));
            }

          	vector<double> next_x_vals;
          	vector<double> next_y_vals;
            // Store the unused old way points to create a smooth path transition
            for(int i=0; i < prev_size; i++){
              next_x_vals.push_back(previous_path_x[i]);
              next_y_vals.push_back(previous_path_y[i]);
            }

            // create a function that intersects all points on the desired path
            tk::spline f_spline;
            f_spline.set_points(ptsx, ptsy);

            double target_x = 30.0;
            double target_y = f_spline(target_x);
            double target_dist = sqrt(target_x*target_x + target_y*target_y);
            // find the number of step points to look the target distance into the future
            // distance(m) = Time_step(s) * desired_vel(Mph) * Mph_to_mps(1609.344/3600 = 0.44704) 
            double N = (target_dist/(0.02*ref_vel*mile_ph_to_meter_ps));
            double max_step = target_x/N;
            double x_local = 0; // the current x point being considered
            
            double acceleration = 0.003; //9 * 0.02; // max allowed acceleration per step
            int way_pts_tot = 50; // how many way points are to be predicted into the future 
            
            double step;
            int next_size = next_x_vals.size();
            // get the previous step between the last two previous trajectory points and set
            // that as the step size to prevent excess acceleration and jerk 
            if(next_size < 2){
              step = 0;
            } else {
              double step_x = next_x_vals[next_size - 1] - next_x_vals[next_size - 2];
              double step_y = next_y_vals[next_size - 1] - next_y_vals[next_size - 2];
              step = sqrt(step_x*step_x + step_y*step_y);
            }
            
            // add new points onto the old way points upto 
            for(int i = 1; i < way_pts_tot - prev_size; i++){
              
              // check if we will potential have a collision and decelerate 
              if(too_close){
                step -= acceleration; // deceleration -5m/s
                if(step < 0){
                  step = 0;
                }
              } else if(car_speed <= max_vel){
                step += acceleration;
                if(step > max_step){
                  step = max_step;
                }
              } 
              
              x_local += step;
              double y_local = f_spline(x_local);
              
              // convert the reference point from local car centric coordinates to global coordinates
              double x_point = x_local * cos(global_yaw) - y_local * sin(global_yaw);
              double y_point = x_local * sin(global_yaw) + y_local * cos(global_yaw);
              
              next_x_vals.push_back(x_point + global_x);
              next_y_vals.push_back(y_point + global_y);
            }
            
          	// TODO: define a path made up of (x,y) points that the car will visit sequentially every .02 seconds
          	msgJson["next_x"] = next_x_vals;
          	msgJson["next_y"] = next_y_vals;

          	auto msg = "42[\"control\","+ msgJson.dump()+"]";

          	//this_thread::sleep_for(chrono::milliseconds(1000));
          	ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
          
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
