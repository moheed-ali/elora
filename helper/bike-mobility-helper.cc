
#include "bike-mobility-helper.h"

// function to read data and store it in vector
std::vector<BikeData> 
readDataset(const std::string& filename) {
    std::vector<BikeData> dataset;
    std::ifstream file(filename);

    if (file) {
        std::string line;
        std::getline(file, line); // Skip header line

        while (std::getline(file, line)) {
            std::stringstream ss(line);
            std::string value;
            BikeData bikeData;

            std::getline(ss, value, ',');
            bikeData.bikeNumber = value;

            std::getline(ss, value, ',');
            bikeData.duration = std::stoi(value);

            std::getline(ss, value, ',');
            bikeData.time_started = std::stol(value);

            std::getline(ss, value, ',');
            bikeData.time_ended = std::stol(value);

            std::getline(ss, value, ',');
            bikeData.start_station = std::stoi(value);

            std::getline(ss, value, ',');
            bikeData.end_station = std::stoi(value);

            std::getline(ss, value, ',');
            bikeData.start_lat = std::stod(value);

            std::getline(ss, value, ',');
            bikeData.start_lng = std::stod(value);

            std::getline(ss, value, ',');
            bikeData.end_lat = std::stod(value);

            std::getline(ss, value);
            bikeData.end_lng = std::stod(value);

            dataset.push_back(bikeData);
        }

        file.close();
    }
    else {
        std::cerr << "Failed to open the file: " << filename << std::endl;
    }

    return dataset;
}

// function to print position
void 
PrintNodePosition(ns3::Ptr<ns3::Node> node) {
    ns3::Ptr<ns3::WaypointMobilityModel> waypointMobility = node->GetObject<ns3::WaypointMobilityModel>();
    ns3::Vector3D position = waypointMobility->GetPosition();
    double time = ns3::Simulator::Now().GetSeconds();
    std::cout << "Node ID : " << node->GetId() << ", Node position at time " << time << ": (" << position.x << ", " << position.y << ", " << position.z << ")" << std::endl;
    std::cout << "Waypoint Left = " << waypointMobility->WaypointsLeft() << std::endl;
    double nWInSec = waypointMobility->GetNextWaypoint().time.GetSeconds();
    std::cout << "Next Waypoint Time = " << nWInSec << std::endl;
    if (waypointMobility->WaypointsLeft() == 0 && waypointMobility->GetVelocity() == ns3::Vector3D(0, 0, 0)) {
        ns3::Simulator::Stop();
    }
    else {
        if (waypointMobility->WaypointsLeft() % 2 != 0) {
            ns3::Simulator::Schedule(ns3::Seconds(nWInSec - time), &PrintNodePosition, node);
        }
        else {
            ns3::Simulator::Schedule(ns3::Seconds(1.0), &PrintNodePosition, node);
        }
    }
}

// function to store unique bike id and give them key
std::map<std::string, int> 
createBikeNumberMap(const std::vector<BikeData>& dataset) {
    std::map<std::string, int> myMap;
    std::set<std::string> uniqueBikeNumbers;

    // Collect unique bike numbers
    for (const BikeData& data : dataset) {
        uniqueBikeNumbers.insert(data.bikeNumber);
    }

    // Assign keys to unique bike numbers and store in the map
    int key = 0;
    for (const std::string& bikeNumber : uniqueBikeNumbers) {
        myMap[bikeNumber] = key++;
    }

    return myMap;
}

ns3::Ptr<ns3::WaypointMobilityModel> 
SaveWaypoints(const std::vector<BikeData>& dataset, const std::map<std::string, int> myMap, ns3::NodeContainer nodes){
    Ptr<ns3::WaypointMobilityModel> waypointMobility;

    int row = 0; 
    for (const BikeData& bike : dataset) {
        for (const auto& pair : myMap) {
            if (pair.first == bike.bikeNumber) {
                // std :: cout << "row = " << row++ << std :: endl;
                if (row == 88563 || row == 149263 || row == 149472 || row ==152101){ // faulty rows
                    std :: cout << "row = " << row << " is Skipped" << std :: endl;
                    row++;
                }
                else{
                    //node = nodes.Get(pair.second);
                    waypointMobility = nodes.Get(pair.second)->GetObject<ns3::WaypointMobilityModel>();
                    // Waypoint 1 - Start Position
                    waypointMobility->AddWaypoint(Waypoint(Seconds(bike.time_started), Vector(bike.start_lng, bike.start_lat, 0.0)));
                    //std :: cout << "Start WayPoint for Node : " << node->GetId() << ", Is Saved for row = " << row << std :: endl; 
                    // Waypoint 2 - End Position                
                    waypointMobility->AddWaypoint(Waypoint(Seconds(bike.time_ended), Vector(bike.end_lng, bike.end_lat, 0.0)));
                    //std :: cout << "End WayPoint for Node : " << node->GetId() << ", Is Saved for row = " << row << std :: endl;
                    row++;
                    //std :: cout << "*****************************************************************" << std :: endl;
                }
            }
        }
    }

    return waypointMobility;
}




