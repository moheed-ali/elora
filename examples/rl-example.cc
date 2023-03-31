/*
 * This program creates a network which uses reinforcement learning
 * to receive devices reconfigurations.
 */

// ns3 imports
#include "ns3/application-container.h"
#include "ns3/command-line.h"
#include "ns3/core-module.h"
#include "ns3/log.h"
#include "ns3/mobility-helper.h"
#include "ns3/okumura-hata-propagation-loss-model.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/propagation-delay-model.h"

// lorawan imports
#include "ns3/basic-energy-source-helper.h"
#include "ns3/forwarder-helper.h"
#include "ns3/hex-grid-position-allocator.h"
#include "ns3/lora-channel.h"
#include "ns3/lora-helper.h"
#include "ns3/lora-radio-energy-model-helper.h"
#include "ns3/network-server-helper.h"
#include "ns3/periodic-sender-helper.h"
#include "ns3/range-position-allocator.h"
#include "ns3/urban-traffic-helper.h"

using namespace ns3;
using namespace lorawan;

NS_LOG_COMPONENT_DEFINE("RlExample");
#include "utilities.cc"

int
main(int argc, char* argv[])
{
    /***************************
     *  Simulation parameters  *
     ***************************/

    int periods = 24; // H * D
    int gatewayRings = 1;
    double range = 2426.85; // Max range for downlink (!) coverage probability > 0.98 (with okumura)
    int nDevices = 100;
    std::string sir = "CROCE";
    bool adrEnabled = false;
    bool initializeSF = true;

    bool rlEnabled = false;
    double target = 0.95;
    std::string clusterStr = "None";
    double changeAfter = 0;
    int newdevs = 0;
    int killdevs = 0;

    bool logs = false;

    /* Expose parameters to command line */
    {
        CommandLine cmd(__FILE__);
        cmd.AddValue("periods", "Number of periods to simulate (1 period = 1 hour)", periods);
        cmd.AddValue("rings", "Number of gateway rings in hexagonal topology", gatewayRings);
        cmd.AddValue("range", "Radius of the device allocation disk around a gateway)", range);
        cmd.AddValue("devices", "Number of end devices to include in the simulation", nDevices);
        cmd.AddValue("sir", "Signal to Interference Ratio matrix used for interference", sir);
        cmd.AddValue("initSF", "Whether to initialize the SFs", initializeSF);
        cmd.AddValue("adr", "Whether to enable online ADR", adrEnabled);
        // Multi-cluster related
        cmd.AddValue("rl",
                     "Use reinfocement learning to receive LoRaWAN downlink re-configurations from "
                     "external source using inter process communication",
                     rlEnabled);
        cmd.AddValue("target", "Central PDR value targeted (single cluster)", target);
        cmd.AddValue("clusters",
                     "Clusters descriptor: {{share,pdr},...} (overrides 'target' param)",
                     clusterStr);
        cmd.AddValue("change",
                     "Time (hours) after which specified devices are (dis)activated",
                     changeAfter);
        cmd.AddValue("add",
                     "Number of devices (from total) that will be activated after time set with "
                     "'change' parameter",
                     newdevs);
        cmd.AddValue("remove",
                     "Number of devices (from total) that will be disabled after time set with "
                     "'change' parameter",
                     killdevs);
        cmd.AddValue("log", "Whether to enable logs", logs);
        cmd.Parse(argc, argv);
        NS_ASSERT((periods > 4) and (gatewayRings > 0) and (nDevices >= 0) and
                  (changeAfter >= 0) and (newdevs >= 0) and (killdevs >= 0));
    }

    /* Apply global configurations */
    {
        Config::SetDefault("ns3::BaseEndDeviceLorawanMac::DRControl",
                           BooleanValue(adrEnabled)); //!< ADR bit
        Config::SetDefault("ns3::RlComponent::StartTime", TimeValue(Hours(2)));
        Config::SetDefault("ns3::RlComponent::EndTime", TimeValue(Hours(periods)));
    }

    /* Logging options */
    if (logs)
    {
        //!> Requirement: build ns3 with debug option
        LogComponentEnable("IpcHandler", LOG_LEVEL_INFO);
        //LogComponentEnable("ClassAEndDeviceLorawanMac", LOG_LEVEL_INFO);
        //LogComponentEnable("BaseEndDeviceLorawanMac", LOG_LEVEL_INFO);
        // LogComponentEnable ("UrbanTrafficHelper", LOG_LEVEL_DEBUG);
        LogComponentEnableAll(LOG_PREFIX_FUNC);
        LogComponentEnableAll(LOG_PREFIX_NODE);
        LogComponentEnableAll(LOG_PREFIX_TIME);
    }

    /******************
     *  Radio Channel *
     ******************/

    Ptr<OkumuraHataPropagationLossModel> loss;
    Ptr<NakagamiPropagationLossModel> rayleigh;
    Ptr<LoraChannel> channel;
    {
        // Delay obtained from distance and speed of light in vacuum (constant)
        Ptr<PropagationDelayModel> delay = CreateObject<ConstantSpeedPropagationDelayModel>();

        // This one is empirical and it encompasses average loss due to distance, shadowing (i.e.
        // obstacles), weather, height
        loss = CreateObject<OkumuraHataPropagationLossModel>();
        loss->SetAttribute("Frequency", DoubleValue(868100000.0));
        loss->SetAttribute("Environment", EnumValue(UrbanEnvironment));
        loss->SetAttribute("CitySize", EnumValue(LargeCity));

        // Here we can add variance to the propagation model with multipath Rayleigh fading
        rayleigh = CreateObject<NakagamiPropagationLossModel>();
        rayleigh->SetAttribute("m0", DoubleValue(1.0));
        rayleigh->SetAttribute("m1", DoubleValue(1.0));
        rayleigh->SetAttribute("m2", DoubleValue(1.0));

        channel = CreateObject<LoraChannel>(loss, delay);
    }

    /**************
     *  Mobility  *
     **************/

    MobilityHelper mobilityEd, mobilityGw;
    Ptr<RangePositionAllocator> rangeAllocator;
    {
        // Gateway mobility
        mobilityGw.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        // In hex tiling, distance = range * cos (pi/6) * 2 to have no holes
        double gatewayDistance = range * std::cos(M_PI / 6) * 2;
        auto hexAllocator = CreateObject<HexGridPositionAllocator>();
        hexAllocator->SetAttribute("Z", DoubleValue(30.0));
        hexAllocator->SetAttribute("distance", DoubleValue(gatewayDistance));
        mobilityGw.SetPositionAllocator(hexAllocator);

        // End Device mobility
        mobilityEd.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        // We define rho to generalize the allocation disk for any number of gateway rings
        double rho = range + 2.0 * gatewayDistance * (gatewayRings - 1);
        rangeAllocator = CreateObject<RangePositionAllocator>();
        rangeAllocator->SetAttribute("rho", DoubleValue(rho));
        rangeAllocator->SetAttribute("ZRV",
                                     StringValue("ns3::UniformRandomVariable[Min=1|Max=10]"));
        rangeAllocator->SetAttribute("range", DoubleValue(range));
        mobilityEd.SetPositionAllocator(rangeAllocator);
    }

    /******************
     *  Create Nodes  *
     ******************/

    Ptr<Node> server;
    NodeContainer gateways;
    NodeContainer endDevices;
    {
        server = CreateObject<Node>();

        int nGateways = 3 * gatewayRings * gatewayRings - 3 * gatewayRings + 1;
        gateways.Create(nGateways);
        mobilityGw.Install(gateways);
        rangeAllocator->SetNodes(gateways);

        endDevices.Create(nDevices);
        mobilityEd.Install(endDevices);
    }

    /************************
     *  Create Net Devices  *
     ************************/

    LoraHelper loraHelper;
    LorawanMacHelper macHelper;
    NetDeviceContainer edNetDevices;
    {
        // PointToPoint links between gateways and server
        PointToPointHelper p2p;
        p2p.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
        p2p.SetChannelAttribute("Delay", StringValue("2ms"));
        for (auto gw = gateways.Begin(); gw != gateways.End(); ++gw)
            p2p.Install(server, *gw);

        /**
         *  LoRa/LoRaWAN layers
         */

        loraHelper.EnablePacketTracking();

        // Create a LoraDeviceAddressGenerator
        uint8_t nwkId = 54;
        uint32_t nwkAddr = 1864;
        auto addrGen = CreateObject<LoraDeviceAddressGenerator>(nwkId, nwkAddr);

        // Mac layer settings
        macHelper.SetRegion(LorawanMacHelper::EU);
        macHelper.SetAddressGenerator(addrGen);

        // Physiscal layer settings
        LoraPhyHelper phyHelper;
        phyHelper.SetInterference("CollisionMatrix", EnumValue(sirMap.at(sir)));
        phyHelper.SetChannel(channel);

        // Create the LoraNetDevices of the gateways
        phyHelper.SetType("ns3::GatewayLoraPhy", "numReceptionPaths", UintegerValue(32));
        macHelper.SetType("ns3::GatewayLorawanMac");
        loraHelper.Install(phyHelper, macHelper, gateways);

        // Create the LoraNetDevices of the end devices
        phyHelper.SetType("ns3::EndDeviceLoraPhy");
        macHelper.SetType("ns3::ClassAEndDeviceLorawanMac");
        edNetDevices = loraHelper.Install(phyHelper, macHelper, endDevices);
    }

    /*************************
     *  Create Applications  *
     *************************/

    cluster_t clusters;
    {
        // Set clusters
        clusters = (clusterStr == "None")
                       ? ParseClusterInfo("{{100.0," + std::to_string(target) + "}}")
                       : ParseClusterInfo(clusterStr);

        // Install the NetworkServer application on the network server
        NetworkServerHelper serverHelper;
        serverHelper.SetEndDevices(endDevices); // Register devices (saves mac layer)
        serverHelper.EnableAdr(adrEnabled);
        serverHelper.EnableRl(rlEnabled);
        serverHelper.AssignClusters(clusters);
        serverHelper.Install(server);

        // Install the Forwarder application on the gateways
        ForwarderHelper forwarderHelper;
        forwarderHelper.Install(gateways);

        // Install applications in EDs
        UrbanTrafficHelper appHelper = UrbanTrafficHelper();
        appHelper.SetDeviceGroups(Commercial);
        auto apps = appHelper.Install(endDevices);

        // Late (dis)activation of devices
        auto i = apps.Begin();
        for (; newdevs > 0 and i != apps.End(); ++i, --newdevs)
            (*i)->SetStartTime(Hours(changeAfter));
        for (; killdevs > 0 and i != apps.End(); ++i, --killdevs)
            (*i)->SetStopTime(Hours(changeAfter));
    }

    /***************************
     *  Simulation and metrics *
     ***************************/

    // Initialize SF emulating the ADR algorithm, then add variance to path loss
    std::vector<int> devPerSF(1, nDevices);
    if (initializeSF)
        devPerSF = macHelper.SetSpreadingFactorsUp(endDevices, gateways, channel);
    //! Here is the point where we allocate CHANNELS in case
    loss->SetNext(rayleigh);

    auto& tracker = loraHelper.GetPacketTracker();
#ifdef NS3_LOG_ENABLE
    // Print current configuration
    PrintConfigSetup(nDevices, range, gatewayRings, devPerSF);
    loraHelper.EnableSimulationTimePrinting(Hours(2));
#endif // NS3_LOG_ENABLE

    // Start simulation
    Simulator::Stop(Hours(periods + 20));
    Simulator::Run();

#ifdef NS3_LOG_ENABLE
    std::cout << tracker.PrintSimulationStatistics(Simulator::Now() - Hours(24));
#endif

    Simulator::Destroy();

    return 0;
}