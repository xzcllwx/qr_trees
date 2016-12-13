//
// Tests the iLQR Tree with LQR parameters to confirm it gives the same answer.
//

#include <ilqr/ilqr_taylor_expansions.hh>
#include <ilqr/ilqr_tree.hh>
#include <ilqr/ilqrtree_helpers.hh>
#include <lqr/LQR.hh>
#include <utils/debug_utils.hh>
#include <utils/helpers.hh>
#include <utils/math_utils.hh>

#include <numeric>

namespace
{

// Tolerance for checking double/floating point equality.
constexpr double WEAKER_TOL = 5e-3;
constexpr double TOL = 1e-5;
constexpr double TIGHTER_TOL = 1e-7;

}

// Initialize iLQR with LQR intialization on a linear dynamics and quadratic
// cost. 
void test_with_lqr_initialization(const int state_dim, 
        const int control_dim, 
        const int T)
{
    std::srand(1);

    // Define the dynamics.
    const Eigen::MatrixXd A = Eigen::MatrixXd::Random(state_dim, state_dim);
    const Eigen::MatrixXd B = Eigen::MatrixXd::Random(state_dim, control_dim);
    const ilqr::DynamicsFunc linear_dyn = create_linear_dynamics(A, B);

    // Define the cost.
    const Eigen::MatrixXd Q = make_random_psd(state_dim, 1e-11);
    const Eigen::MatrixXd R = make_random_psd(control_dim, 1e-1);
    const ilqr::CostFunc quad_cost = create_quadratic_cost(Q, R);

    // Create a list of initial states for the iLQR. 
    Eigen::VectorXd x0 = Eigen::VectorXd::Random(state_dim);
    
    // Storage for regular LQR.
    std::vector<Eigen::VectorXd> lqr_states, lqr_controls;
    std::vector<double> lqr_costs;

    // Compute the true LQR result.
    lqr::LQR lqr(A, B, Q, R, T);
    lqr.solve();
    lqr.forward_pass(x0, lqr_costs, lqr_states, lqr_controls);

    // Storage for iLQR
    std::vector<Eigen::VectorXd> ilqr_states, ilqr_controls;
    std::vector<double> ilqr_costs;
    ilqr::iLQRTree ilqr_tree(state_dim, control_dim);
    std::vector<ilqr::TreeNodePtr> ilqr_chain = ilqr::construct_chain(lqr_states, lqr_controls,
        linear_dyn, quad_cost, ilqr_tree);
    ilqr_tree.bellman_tree_backup();
    ilqr_tree.forward_tree_update(1.0);
    ilqr::get_forward_pass_info(ilqr_chain, quad_cost, ilqr_states, ilqr_controls, ilqr_costs);

    // Recompute costs to check that the costs being returned are correct.
    double ilqr_cost = 0;
    double lqr_cost = 0;
    for (int t = 0; t < T; ++t)
    {
        const Eigen::VectorXd lqr_x = lqr_states[t];
        const Eigen::VectorXd lqr_u = lqr_controls[t];
        const Eigen::VectorXd ilqr_x = ilqr_states[t];
        const Eigen::VectorXd ilqr_u = ilqr_controls[t];
        //PRINT("t=" << t << ": ilqr_x " << ilqr_x.transpose());
        //WARN("   : lqr_x " << lqr_x.transpose());
        //WARN("   : ilqr_u " << ilqr_u.transpose());
        //WARN("   : lqr_u " << lqr_u.transpose());

        IS_TRUE(math::is_equal(lqr_x, ilqr_x, TOL));
        IS_TRUE(math::is_equal(lqr_u, ilqr_u, TOL));

        ilqr_cost += quad_cost(ilqr_x, ilqr_u);
        lqr_cost += quad_cost(lqr_x, lqr_u);
        IS_ALMOST_EQUAL(quad_cost(ilqr_x, ilqr_u), ilqr_costs[t], TOL);
        IS_ALMOST_EQUAL(ilqr_costs[t], lqr_costs[t], TOL);
        IS_ALMOST_EQUAL(ilqr_cost, lqr_cost, TOL);
    }

    const double lqr_total_cost = std::accumulate(lqr_costs.begin(), lqr_costs.end(), 0.0);
    const double ilqr_total_cost = std::accumulate(ilqr_costs.begin(), ilqr_costs.end(), 0.0);
    IS_ALMOST_EQUAL(lqr_total_cost, ilqr_total_cost, TOL);
    IS_ALMOST_EQUAL(lqr_total_cost, lqr_cost, TOL);
    IS_ALMOST_EQUAL(ilqr_total_cost, ilqr_cost, TOL);

    // Confirm another backwards and forwards pass does not change the results.
    std::vector<Eigen::VectorXd> ilqr_states_2, ilqr_controls_2;
    std::vector<double> ilqr_costs_2;
    ilqr_tree.bellman_tree_backup();
    ilqr_tree.forward_tree_update(1.0);
    ilqr::get_forward_pass_info(ilqr_chain, quad_cost, ilqr_states_2, ilqr_controls_2, ilqr_costs_2);
    const double ilqr_total_cost_2 = std::accumulate(ilqr_costs_2.begin(), ilqr_costs_2.end(), 0.0);
    IS_ALMOST_EQUAL(ilqr_total_cost_2, ilqr_total_cost, TIGHTER_TOL);
    IS_TRUE(std::equal(ilqr_costs.begin(), ilqr_costs.end(), 
                ilqr_costs_2.begin(), [](const double &a, const double &b) { 
                return std::abs(a-b) <= TOL;
            }));
    IS_TRUE(std::equal(ilqr_states.begin(), ilqr_states.end(), ilqr_states_2.begin(), 
            [](const Eigen::VectorXd &a, const Eigen::VectorXd &b) { 
                return math::is_equal(a, b, TOL);
            }));
    IS_TRUE(std::equal(ilqr_controls.begin(), ilqr_controls.end(), ilqr_controls_2.begin(), 
            [](const Eigen::VectorXd &a, const Eigen::VectorXd &b) { 
                return math::is_equal(a, b, TOL);
            }));
}

// iLQR even initialized at different states and controls than the true LQR 
// should converge on 1 iteration (perfect Newton step).
void test_converge_to_lqr(const int state_dim, const int control_dim, const int T)
{
    std::srand(2);

    // Define the dynamics.
    const Eigen::MatrixXd A = Eigen::MatrixXd::Random(state_dim, state_dim);
    const Eigen::MatrixXd B = Eigen::MatrixXd::Random(state_dim, control_dim);
    //const Eigen::MatrixXd A = Eigen::MatrixXd::Identity(state_dim, state_dim);
    //const Eigen::MatrixXd B = Eigen::MatrixXd::Identity(state_dim, control_dim);
    const ilqr::DynamicsFunc linear_dyn = create_linear_dynamics(A, B);

    // Define the cost.
    const Eigen::MatrixXd Q = make_random_psd(state_dim, 1e-11);
    const Eigen::MatrixXd R = make_random_psd(control_dim, 1e-3);
    //const Eigen::MatrixXd Q = Eigen::MatrixXd::Identity(state_dim, state_dim);
    //const Eigen::MatrixXd R = Eigen::MatrixXd::Identity(control_dim, control_dim);
    const ilqr::CostFunc quad_cost = create_quadratic_cost(Q, R);


    // Create a list of initial states for the iLQR. 
    Eigen::VectorXd x0 = Eigen::VectorXd::Random(state_dim);
    
    // Storage for regular LQR.
    std::vector<Eigen::VectorXd> lqr_states, lqr_controls;
    std::vector<double> lqr_costs;

    // Compute the true LQR result.
    lqr::LQR lqr(A, B, Q, R, T);
    lqr.solve();
    lqr.forward_pass(x0, lqr_costs, lqr_states, lqr_controls);

    // Storage for iLQR
    std::vector<Eigen::VectorXd> ilqr_init_states, ilqr_init_controls;
    std::vector<Eigen::VectorXd> ilqr_states, ilqr_controls;
    std::vector<double> ilqr_costs;
    Eigen::VectorXd xt = x0;
    for (int t = 0; t < T; ++t)
    {
        ilqr_init_states.push_back(xt);
        const Eigen::VectorXd ut = Eigen::VectorXd::Random(control_dim);
        ilqr_init_controls.push_back(ut);
        xt = linear_dyn(xt, ut);
    }

    ilqr::iLQRTree ilqr_tree(state_dim, control_dim);
    std::vector<ilqr::TreeNodePtr> ilqr_chain = ilqr::construct_chain(ilqr_init_states, ilqr_init_controls,
        linear_dyn, quad_cost, ilqr_tree);
    ilqr_tree.bellman_tree_backup();
    ilqr_tree.forward_tree_update(1.0);
    ilqr::get_forward_pass_info(ilqr_chain, quad_cost, ilqr_states, ilqr_controls, ilqr_costs);

    // Recompute costs to check that the costs being returned are correct.
    double ilqr_cost = 0;
    double lqr_cost = 0;
    for (int t = 0; t < T; ++t)
    {
        const Eigen::VectorXd lqr_x = lqr_states[t];
        const Eigen::VectorXd lqr_u = lqr_controls[t];
        const Eigen::VectorXd ilqr_x = ilqr_states[t];
        const Eigen::VectorXd ilqr_u = ilqr_controls[t];

        
        //PRINT("t=" << t << ": ilqr_x " << ilqr_x.transpose());
        //WARN("   : lqr_x " << lqr_x.transpose());
        //WARN("   : ilqr_x_orig " << ilqr_init_states[t].transpose());
        //WARN("   : ilqr_u " << ilqr_u.transpose());
        //WARN("   : lqr_u " << lqr_u.transpose());

        //if (t < T-1)
        //{
        //    auto xt1_lqr = linear_dyn(lqr_x, lqr_u);
        //    auto xt1_ilqr = ilqr_chain[t]->item()->dynamics_func()(ilqr_x, ilqr_u);
        //    WARN("   : ilqr_xt1 " << xt1_ilqr.transpose());
        //    WARN("   : lqr_xt1 " << xt1_lqr.transpose());
        //}
        

        IS_TRUE(math::is_equal(lqr_x, ilqr_x, WEAKER_TOL));
        IS_TRUE(math::is_equal(lqr_u, ilqr_u, WEAKER_TOL));


        ilqr_cost += quad_cost(ilqr_x, ilqr_u);
        lqr_cost += quad_cost(lqr_x, lqr_u);
        IS_ALMOST_EQUAL(quad_cost(ilqr_x, ilqr_u), ilqr_costs[t], TOL);
        IS_ALMOST_EQUAL(ilqr_costs[t], lqr_costs[t], WEAKER_TOL);
        IS_ALMOST_EQUAL(ilqr_cost, lqr_cost, WEAKER_TOL);
    }

    const double lqr_total_cost = std::accumulate(lqr_costs.begin(), lqr_costs.end(), 0.0);
    const double ilqr_total_cost = std::accumulate(ilqr_costs.begin(), ilqr_costs.end(), 0.0);
    IS_ALMOST_EQUAL(lqr_total_cost, ilqr_total_cost, TOL);
    IS_ALMOST_EQUAL(lqr_total_cost, lqr_cost, TOL);
    IS_ALMOST_EQUAL(ilqr_total_cost, ilqr_cost, TOL);

    // Confirm another backwards and forwards pass does not change the results.
    std::vector<Eigen::VectorXd> ilqr_states_2, ilqr_controls_2;
    std::vector<double> ilqr_costs_2;
    ilqr_tree.bellman_tree_backup();
    ilqr_tree.forward_tree_update(1.0);
    get_forward_pass_info(ilqr_chain, quad_cost, ilqr_states_2, ilqr_controls_2, ilqr_costs_2);
    const double ilqr_total_cost_2 = std::accumulate(ilqr_costs_2.begin(), ilqr_costs_2.end(), 0.0);
    IS_ALMOST_EQUAL(ilqr_total_cost_2, ilqr_total_cost, TIGHTER_TOL);
    IS_TRUE(std::equal(ilqr_costs.begin(), ilqr_costs.end(), 
                ilqr_costs_2.begin(), [](const double &a, const double &b) { 
                return std::abs(a-b) <= TOL;
            }));
    IS_TRUE(std::equal(ilqr_states.begin(), ilqr_states.end(), ilqr_states_2.begin(), 
            [](const Eigen::VectorXd &a, const Eigen::VectorXd &b) { 
                return math::is_equal(a, b, WEAKER_TOL);
            }));

    IS_TRUE(std::equal(ilqr_controls.begin(), ilqr_controls.end(), ilqr_controls_2.begin(), 
            [](const Eigen::VectorXd &a, const Eigen::VectorXd &b) { 
                return math::is_equal(a, b, WEAKER_TOL);
            }));
}

int main()
{
    // Should work with square and non-square dimensions. 
    // Should work with many timesteps.
    test_with_lqr_initialization(5, 2, 8);
    test_with_lqr_initialization(5, 5, 2);
    test_with_lqr_initialization(5, 2, 2);
    test_with_lqr_initialization(5, 2, 8);
    test_with_lqr_initialization(5, 2, 150);
    test_with_lqr_initialization(1, 1, 150);
    test_with_lqr_initialization(1, 1, 2);
    // Should not work with only 1 timstep. 
    DOES_THROW(test_with_lqr_initialization(3, 2, 1));

    test_converge_to_lqr(8,2,7);
    test_converge_to_lqr(5,5,8);
    test_converge_to_lqr(3,2,4);
    test_converge_to_lqr(3,2,8);
    test_converge_to_lqr(3,2,50);
    test_converge_to_lqr(1,1,8);
    // Should not work with only 1 timstep. 
    DOES_THROW(test_converge_to_lqr(3, 2, 1));
}

