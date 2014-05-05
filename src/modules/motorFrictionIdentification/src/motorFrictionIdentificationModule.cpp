/*
 * Copyright (C) 2013 CoDyCo
 * Author: Andrea Del Prete
 * email:  andrea.delprete@iit.it
 * Permission is granted to copy, distribute, and/or modify this program
 * under the terms of the GNU General Public License, version 2 or any
 * later version published by the Free Software Foundation.
 *
 * A copy of the license can be found at
 * http://www.robotcub.org/icub/license/gpl.txt
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details
*/

#include <iostream>
#include <sstream>
#include <iomanip>
#include <string.h>

#include <yarp/os/Log.h>

#include "motorFrictionIdentificationLib/motorFrictionIdentificationParams.h"
#include "motorFrictionIdentificationLib/jointTorqueControlParams.h"
#include "motorFrictionIdentification/motorFrictionIdentificationModule.h"

using namespace yarp::dev;
using namespace paramHelp;
using namespace wbiIcub;
using namespace motorFrictionIdentification;

MotorFrictionIdentificationModule::MotorFrictionIdentificationModule()
{
    identificationThread    = 0;
    robotInterface          = 0;
    paramHelper             = 0;
    modulePeriod            = MODULE_PERIOD;
    threadPeriod            = 0;
}

void iCubVersionFromRf(ResourceFinder & rf, iCub::iDynTree::iCubTree_version_tag & icub_version)
{
    //Checking iCub parts version
    /// \todo this part should be replaced by a more general way of accessing robot parameters
    ///       namely urdf for structure parameters and robotInterface xml (or runtime interface) to get available sensors
    icub_version.head_version = 2;
    if( rf.check("headV1") ) {
        icub_version.head_version = 1;
    }
    if( rf.check("headV2") ) {
        icub_version.head_version = 2;
    }

    icub_version.legs_version = 2;
    if( rf.check("legsV1") ) {
        icub_version.legs_version = 1;
    }
    if( rf.check("legsV2") ) {
        icub_version.legs_version = 2;
    }

    /// \note if feet_version are 2, the presence of FT sensors in the feet is assumed
    icub_version.feet_ft = true;
    if( rf.check("feetV1") ) {
        icub_version.feet_ft = false;
    }
    if( rf.check("feetV2") ) {
        icub_version.feet_ft = true;
    }

    #ifdef CODYCO_USES_URDFDOM
    if( rf.check("urdf") )
    {
        icub_version.uses_urdf = true;
        icub_version.urdf_file = rf.find("urdf").asString().c_str();
    }
    #endif
}

bool MotorFrictionIdentificationModule::configure(ResourceFinder &rf)
{


    //--------------------------PARAMETER HELPER--------------------------
    paramHelper = new ParamHelperServer(motorFrictionIdentificationParamDescr, PARAM_ID_SIZE,
                                        motorFrictionIdentificationCommandDescr, COMMAND_ID_SIZE);
    paramHelper->linkParam(PARAM_ID_MODULE_NAME,    &moduleName);
    paramHelper->linkParam(PARAM_ID_CTRL_PERIOD,    &threadPeriod);
    paramHelper->linkParam(PARAM_ID_ROBOT_NAME,     &robotName);
    paramHelper->registerCommandCallback(COMMAND_ID_HELP, this);
    paramHelper->registerCommandCallback(COMMAND_ID_QUIT, this);

    // Read parameters from configuration file (or command line)
    Bottle initMsg;
    paramHelper->initializeParams(rf, initMsg);
    printBottle(initMsg);

    ///< PARAMETER HELPER CLIENT (FOR JOINT TORQUE CONTROL)
    const ParamProxyInterface* torqueCtrlParams[5];
    torqueCtrlParams[0] = jointTorqueControl::jointTorqueControlParamDescr[4];
    torqueCtrlParams[1] = jointTorqueControl::jointTorqueControlParamDescr[5];
    torqueCtrlParams[2] = jointTorqueControl::jointTorqueControlParamDescr[6];
    torqueCtrlParams[3] = jointTorqueControl::jointTorqueControlParamDescr[7];
    torqueCtrlParams[4] = jointTorqueControl::jointTorqueControlParamDescr[8];
    YARP_ASSERT(torqueCtrlParams[0]->id==jointTorqueControl::PARAM_ID_KT);
    torqueController = new ParamHelperClient(torqueCtrlParams, 5); //jointTorqueControl::PARAM_ID_SIZE);

    // do not initialize paramHelperClient because jointTorqueControl module may not be running

    // Open ports for communicating with other modules
    if(!paramHelper->init(moduleName))
    {
        fprintf(stderr, "Error while initializing parameter helper. Closing module.\n");
        return false;
    }
    rpcPort.open(("/"+moduleName+"/rpc").c_str());
    setName(moduleName.c_str());
    attach(rpcPort);

    //--------------------------WHOLE BODY INTERFACE--------------------------
    iCub::iDynTree::iCubTree_version_tag icub_version;
    iCubVersionFromRf(rf,icub_version);
    robotInterface = new icubWholeBodyInterface(moduleName.c_str(),
                                                robotName.c_str(),
                                                icub_version);
    ///< read the parameter "joint list" from configuration file to configure the WBI
    jointList.resize(paramHelper->getParamProxy(PARAM_ID_JOINT_LIST)->size);
    paramHelper->getParamProxy(PARAM_ID_JOINT_LIST)->linkToVariable(jointList.data());
    ///< link the parameter "joint names" to the variable jointNames
    jointNames.resize(jointList.size());
    paramHelper->getParamProxy(PARAM_ID_JOINT_NAMES)->linkToVariable(jointNames.data(), jointList.size());
    ///< add all the specified joints
    bool ok = true;
    LocalId lid;
    for(int i=0; ok && i<jointList.size(); i++)
    {
        lid = globalToLocalIcubId(jointList[i]);
        ok = robotInterface->addJoint(lid);
        jointNames[i] = lid.description;
    }

    if(!ok || !robotInterface->init())
    {
        fprintf(stderr, "Error while initializing whole body interface. Closing module\n");
        return false;
    }
fprintf(stderr, "After initialize interface\n");
    //--------------------------CTRL THREAD--------------------------
    identificationThread = new MotorFrictionIdentificationThread(moduleName, robotName, threadPeriod, paramHelper, robotInterface, torqueController);
    if(!identificationThread->start())
    {
        fprintf(stderr, "Error while initializing motorFrictionIdentification control thread. Closing module.\n");
        return false;
    }

    fprintf(stderr,"MotorFrictionIdentification control started\n");
	return true;
}

bool MotorFrictionIdentificationModule::respond(const Bottle& cmd, Bottle& reply)
{
    paramHelper->lock();
    if(!paramHelper->processRpcCommand(cmd, reply))
        reply.addString( (string("Command ")+cmd.toString().c_str()+" not recognized.").c_str());
    paramHelper->unlock();

    // if reply is empty put something into it, otherwise the rpc communication gets stuck
    if(reply.size()==0)
        reply.addString( (string("Command ")+cmd.toString().c_str()+" received.").c_str());
	return true;
}

void MotorFrictionIdentificationModule::commandReceived(const CommandDescription &cd, const Bottle &params, Bottle &reply)
{
    switch(cd.id)
    {
    case COMMAND_ID_HELP:
        paramHelper->getHelpMessage(reply);
        break;
    case COMMAND_ID_QUIT:
        stopModule();
        reply.addString("Quitting module.");
        break;
    }
}

bool MotorFrictionIdentificationModule::interruptModule()
{
    ///< This method is called by the stopModule method
    return true;
}

bool MotorFrictionIdentificationModule::close()
{
    ///< This method is called by the module thread, which is not the same managing the
    ///< RPC calls
    if(identificationThread)
    {
        identificationThread->suspend();
        identificationThread->stop();
        delete identificationThread;
        identificationThread = 0;
    }
    if(paramHelper)
    {
        paramHelper->close();
        delete paramHelper;
        paramHelper = 0;
    }
    if(robotInterface)
    {
        bool res=robotInterface->close();
        if(res)
            printf("Error while closing robot interface\n");
        delete robotInterface;
        robotInterface = 0;
    }

	//closing ports
    rpcPort.interrupt();
	rpcPort.close();

    printf("[PERFORMANCE INFORMATION]:\n");
    printf("Expected period %d ms.\nReal period: %3.1f+/-%3.1f ms.\n", threadPeriod, avgTime, stdDev);
    printf("Real duration of 'run' method: %3.1f+/-%3.1f ms.\n", avgTimeUsed, stdDevUsed);
    if(avgTimeUsed<0.5*threadPeriod)
        printf("Next time you could set a lower period to improve the controller performance.\n");
    else if(avgTime>1.3*threadPeriod)
        printf("The period you set was impossible to attain. Next time you could set a higher period.\n");

    return true;
}

bool MotorFrictionIdentificationModule::updateModule()
{
    if (identificationThread==0)
    {
        printf("ControlThread pointers are zero\n");
        return false;
    }

    identificationThread->getEstPeriod(avgTime, stdDev);
    identificationThread->getEstUsed(avgTimeUsed, stdDevUsed);     // real duration of run()
//#ifndef NDEBUG
    if(avgTime > 1.3 * threadPeriod)
    {
        printf("[WARNING] Control loop is too slow. Real period: %3.3f+/-%3.3f. Expected period %d.\n", avgTime, stdDev, threadPeriod);
        printf("Duration of 'run' method: %3.3f+/-%3.3f.\n", avgTimeUsed, stdDevUsed);
    }
//#endif

    return true;
}
