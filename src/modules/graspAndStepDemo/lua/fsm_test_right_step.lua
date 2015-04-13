
fsm_right_step = rfsm.state {
    ST_DOUBLESUPPORT_INITIAL_COM = rfsm.state{
        entry=function()
            -- set the odometry fixed link to the right foot
            gas_activateConstraints(constraints_port,{'r_foot','l_foot'})

            gas_sendStringsToPort(odometry_port,"changeFixedLinkSimpleLeggedOdometry","l_foot");

            gas_sendCOMToTrajGen(setpoints_port,gas_setpoints.initial_com_in_world);
        end,
    },

    ST_DOUBLESUPPORT_TRANSFER_WEIGHT_TO_LEFT_FOOT = rfsm.state{
        entry=function()
            gas_sendCOMToTrajGen(setpoints_port,gas_setpoints.weight_on_left_foot_com_in_world);
        end,
    },

    ST_SINGLESUPPORT_ON_LEFT_FOOT = rfsm.state{
        entry=function()
            gas_deactivateConstraints(constraints_port,{'r_foot'})

            -- Generate foot setpoints (two setpoints)
            gas_generate_right_foot_setpoints()
        end,
    },

    ST_SINGLESUPPORT_RIGHT_FOOT_INITIAL_SWING = rfsm.state{
        entry=function()
            -- Send initial r_sole setpoint
            root_link_r_foot_des_pos = gas_get_transform(root_link,"world"):compose( gas_setpoints.world_r_foot_initial_swing_des_pos)

            right_leg_qdes = query_cartesian_solver(root_link_r_sole_solver_port,root_link_r_foot_des_pos);

            -- send desired q to right_leg
            gas_sendPartToTrajGen("right_leg",right_leg_qdes)
        end,
    }

    ST_SINGLESUPPORT_RIGHT_FOOT_FINAL_SWING = rfsm.state{
        entry=function()
            gas_sendCOMToTrajGen(setpoints_port,gas_setpoints.weight_on_left_foot_com_in_world);

            -- Send final r_sole setpoint
            root_link_r_foot_des_pos = gas_get_transform(root_link,"world"):compose( gas_setpoints.world_r_foot_final_swing_des_pos)

            right_leg_qdes = query_cartesian_solver(root_link_r_sole_solver_port,root_link_r_foot_des_pos);

            -- send desired q to right_leg
            gas_sendPartToTrajGen("right_leg",right_leg_qdes)
        end,
    }

    ST_DOUBLESUPPORT_AFTER_STEP = rfsm.state{
        entry=function()
            -- reactivate constraint on r_foot
            gas_activateConstraints(constraints_port,{'r_foot','l_foot'})

        end,
    }

    ----------------------------------
    -- setting the transitions      --
    ----------------------------------C

    -- Initial transition
    rfsm.transition { src='initial', tgt='ST_DOUBLESUPPORT_INITIAL_COM' },

    -- Sensor transitions
    rfsm.transition { src='ST_DOUBLESUPPORT_INITIAL_COM', tgt='ST_DOUBLESUPPORT_TRANSFER_WEIGHT_TO_LEFT_FOOT', events={ 'e_right_step_requested' } },
    rfsm.transition { src='ST_DOUBLESUPPORT_TRANSFER_WEIGHT_TO_LEFT_FOOT', tgt='ST_SINGLESUPPORT_ON_LEFT_FOOT', events={ 'e_com_motion_done' } },
    rfsm.transition { src='ST_SINGLESUPPORT_ON_LEFT_FOOT', tgt='ST_DOUBLESUPPORT_INITIAL_COM', events={ 'e_reset' } },
    rfsm.transition { src='ST_SINGLESUPPORT_ON_LEFT_FOOT', tgt='ST_SINGLESUPPORT_RIGHT_FOOT_INITIAL_SWING', events={ 'e_dbg_go_on'} },
    rfsm.transition { src='ST_SINGLESUPPORT_RIGHT_FOOT_INITIAL_SWING', tgt='ST_SINGLESUPPORT_RIGHT_FOOT_FINAL_SWING', events={ 'e_dbg_go_on'} },
    rfsm.transition { src='ST_SINGLESUPPORT_RIGHT_FOOT_FINAL_SWING', tgt='ST_DOUBLESUPPORT_AFTER_STEP', events={ 'e_dbg_go_on' } },

}

return fsm_right_step

