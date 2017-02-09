#!/usr/bin/env python

#
# Shervin Javdani (sjavdani@cs.cmu.edu)
# January 2017
#

# If we are not running from the build directory, then add lib to path from
# build assuming we are running from the python folder
import os
full_path = os.path.realpath(__file__)
if full_path.count("src/python") > 0:
    import sys
    to_add = os.path.abspath(os.path.join(os.path.split(full_path)[0], "../../build/"))
    sys.path.append(to_add)

from IPython import embed

import lib.shared_autonomy_circle_class_bindings as ilqr
import visualize_circle_world as vis

import numpy as np
import matplotlib.pyplot as plt

import cPickle as pickle
import argparse

import multiprocessing as mp
from functools import partial



data_dir = "../cached_data"
pckl_filename = "shared_autonomy.pckl"


class CachedDataKeys:
  def __init__(self):
    self.labels = 'labels'
    self.states = 'states'
    self.values = 'values_grid'
    self.num_timesteps = 'num_timesteps'
    self.positions_get_value = 'positions_get_value'
    self.obs_poses = 'obs_poses'
    self.obs_radii = 'obs_radii'
    self.start_pos = 'start_pos'
    self.goal_states = 'goal_states'
    self.goal_priors = 'goal_priors'
    self.true_goal_ind = 'true_goal_ind'
    self.world_dims = 'world_dims'

cached_data_keys = CachedDataKeys()

def save_pckl(data):
  filename = data_dir + '/' + pckl_filename
  with open(filename, 'w') as file:
    pickle.dump(data, file)

def load_pckl():
  filename = data_dir + '/' + pckl_filename
  if os.path.isfile(filename):
    with open(filename, 'r') as file:
      return pickle.load(file)
  else:
    return None

def run_controller_until_done(policy_type, init_args):
  init_keys = CachedDataKeys()
  world_dims = init_args[init_keys.world_dims]
  positions_get_value = init_args[init_keys.positions_get_value]

  world = ilqr.CircleWorld(world_dims)

  obs_pos_1 = init_args[init_keys.obs_poses][0]
  obs_radius = init_args[init_keys.obs_radii][0]
  obstacle_1 = ilqr.Circle(obs_radius, obs_pos_1);

  # add obstacle to world 1
  world.add_obstacle(obstacle_1);

  goal_states = init_args[init_keys.goal_states]
  goal_priors = init_args[init_keys.goal_priors]

  true_goal_ind = init_args[init_keys.true_goal_ind]
  num_timesteps = init_args[init_keys.num_timesteps]

  controller = ilqr.SharedAutonomyCircle(policy_type, world, goal_states, goal_priors, true_goal_ind, num_timesteps)

  all_value_grids = []
  while not controller.is_done():
    controller.run_control(1)
    values = controller.get_values_at_positions(positions_get_value, 0)
    all_value_grids.append(values)

  return (controller.get_states(), all_value_grids)



if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run shared autonomy ilqr")
    parser.add_argument("--rerun", action="store_true", help="rerun the shared autonomy even if cached data exists")

    args = parser.parse_args()

    all_vals_plotting = load_pckl()
    if all_vals_plotting is None or args.rerun:
      all_vals_plotting = {}
      world_dims = [-30, 30, -30, 30]
      all_vals_plotting[cached_data_keys.world_dims] = world_dims

      #set positions for getting value
      dist_between = 5.
      positions_get_value = []
      for x in np.arange( float(world_dims[0])+dist_between, float(world_dims[1]), dist_between):
        for y in np.arange( float(world_dims[2])+dist_between, float(world_dims[3]), dist_between):
          positions_get_value.append(np.array([x, y]))
      
      all_vals_plotting[cached_data_keys.positions_get_value] = positions_get_value

      obs_poses = [np.array([0., 0.])]
      obs_radii = [10.0]
      all_vals_plotting[cached_data_keys.obs_poses] = obs_poses
      all_vals_plotting[cached_data_keys.obs_radii] = obs_radii

      goal_states = []
      if ilqr.State.STATE_DIM == 4:
        goal_states.append(np.array([-15., 25., 0., 0.]))
        goal_states.append(np.array([15, 25., 0., 0.]))
      else:
        goal_states.append(np.array([-15., 25.]))
        goal_states.append(np.array([15, 25.]))
      goal_priors = [0.5, 0.5]

      true_goal_ind = 0
      num_timesteps = 30

      all_vals_plotting[cached_data_keys.goal_states] = goal_states
      all_vals_plotting[cached_data_keys.goal_priors] = goal_priors
      all_vals_plotting[cached_data_keys.true_goal_ind] = true_goal_ind
      all_vals_plotting[cached_data_keys.num_timesteps] = num_timesteps
      #all_vals_plotting[start_pos_key] = start_pos

    
      policy_types = [ilqr.TRUE_ILQR, ilqr.HINDSIGHT, ilqr.PROB_WEIGHTED_CONTROL, ilqr.AVG_COST]
      labels = [str(p) for p in policy_types]
      all_vals_plotting[cached_data_keys.labels] = labels
      #create all controllers
      p = mp.Pool(4)


      all_return_values = p.map(partial(run_controller_until_done, init_args=all_vals_plotting), policy_types)

      for policy_type,label,controller_returns in zip(policy_types, labels, all_return_values):
        #make a dict with relevant values for this controller
        vals_this_cont = {cached_data_keys.states: controller_returns[0], cached_data_keys.values: controller_returns[1]}

        #save to dict of all values
        all_vals_plotting[label] = vals_this_cont


      all_vals_plotting[cached_data_keys.start_pos] = all_return_values[0][0][0]
      save_pckl(all_vals_plotting)  



    COLORS = [(0.3,0.3,0.3, 0.2), (0.1,0.8,0.8, 0.2), (0.1,0.3,0.8, 0.2), (0.8,0.3,0.8, 0.2), (0.7,0.8,0.2, 0.2)] 

    # draw
    plt.figure(figsize=(10, 8))
    ax = plt.gca()

    positions_get_value = all_vals_plotting[cached_data_keys.positions_get_value]
    labels = all_vals_plotting[cached_data_keys.labels]

    obs_poses = all_vals_plotting[cached_data_keys.obs_poses]
    obs_radii = all_vals_plotting[cached_data_keys.obs_radii]
    start_pos = all_vals_plotting[cached_data_keys.start_pos]
    goal_states = all_vals_plotting[cached_data_keys.goal_states]
    true_goal_ind = all_vals_plotting[cached_data_keys.true_goal_ind]
    world_dims = all_vals_plotting[cached_data_keys.world_dims]



    #TODO darken points for current time step
    #vis.draw_value_at_positions(ax, positions_get_value, all_vals_plotting[labels[0]][cached_data_keys.values][1])

    vis.draw_env_multiend(ax, start_pos, goal_states, true_goal_ind, vis.robot_radius, obs_poses, obs_radii);
    labels_circ = []
    for label, color in zip(labels, COLORS):
        states = all_vals_plotting[label][cached_data_keys.states]
        label_circ = vis.draw_traj(ax, states, vis.robot_radius, color=color, label=label, skip=1)
        labels_circ.append(label_circ)
    plt.axis('square')
    plt.axis([float(d) for d in world_dims])
    ax.legend(handles=labels_circ)
    plt.tight_layout()
    plt.show()

#    cost, states_true_1, obs_fname_1 = ilqr.control_shared_autonomy(ilqr.TRUE_ILQR, 
#            world, goal_states, goal_priors, 0, "true1", "true1")
#    cost, states_true_2, obs_fname_2 = ilqr.control_shared_autonomy(ilqr.TRUE_ILQR, 
#            world, goal_states, goal_priors, 1, "true2", "true2")
#    #cost, states_true_2, obs_fname_2 = ilqr.control_shared_autonomy(ilqr.TRUE_ILQR, 
#
#    cost, states_weighted_1, obs_fname_3 = ilqr.control_shared_autonomy(ilqr.PROB_WEIGHTED_CONTROL, 
#            world, goal_states, goal_priors, 0, "weight3", "weight3")
#    cost, states_weighted_2, obs_fname_4 = ilqr.control_shared_autonomy(ilqr.PROB_WEIGHTED_CONTROL, 
#            world, goal_states, goal_priors, 1, "weight4", "weight4")
#
#    cost, states_hind_1, obs_fname_5 = ilqr.control_shared_autonomy(ilqr.HINDSIGHT, 
#            world, goal_states, goal_priors, 0, "hind3", "hind3")
#    cost, states_hind_2, obs_fname_6 = ilqr.control_shared_autonomy(ilqr.HINDSIGHT, 
#            world, goal_states, goal_priors, 1, "hind4", "hind4")
#
#    
#    print("Drawing world 1")
#    ax1 = vis.parse_draw_files([states_true_1, states_weighted_1, states_hind_1], obs_fname_1,
#            show=False) 
#    plt.title('World 1')
#
#    print("Drawing world 2")
#    ax2 = vis.parse_draw_files([states_true_2, states_weighted_2, states_hind_2], 
#            obs_fname_2, show=False) 
#    plt.title('World 2')
#    plt.show()
#
    #embed()


