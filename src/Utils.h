#pragma once

#include "ForwardRayTracing.h"

#include "Broyden.h"

#include <optional>
#include <oneapi/tbb.h>
#include <Eigen/Dense>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry/geometries/box.hpp>
#include <boost/geometry/index/rtree.hpp>

template <typename Real, typename Complex>
struct SweepResult
{
    using PointVector = Eigen::Matrix<Real, Eigen::Dynamic, 2>;
    using Matrix = Eigen::Matrix<Real, Eigen::Dynamic, Eigen::Dynamic>;

    Matrix theta;
    Matrix phi;

    Matrix lambda;
    Matrix eta;

    Matrix delta_theta;
    Matrix delta_phi;

    PointVector theta_roots;
    PointVector phi_roots;

    PointVector theta_roots_closest;

    std::vector<ForwardRayTracingResult<Real, Complex>> results;
};

template <typename LReal, typename LComplex, typename Real, typename Complex>
SweepResult<LReal, LComplex> get_low_prec(const SweepResult<Real, Complex> &x)
{
    SweepResult<LReal, LComplex> result;
    result.theta = x.theta.template cast<LReal>();
    result.phi = x.phi.template cast<LReal>();
    result.lambda = x.lambda.template cast<LReal>();
    result.eta = x.eta.template cast<LReal>();
    result.delta_theta = x.delta_theta.template cast<LReal>();
    result.delta_phi = x.delta_phi.template cast<LReal>();
    result.theta_roots = x.theta_roots.template cast<LReal>();
    result.phi_roots = x.phi_roots.template cast<LReal>();
    result.theta_roots_closest = x.theta_roots_closest.template cast<LReal>();
    result.results.reserve(x.results.size());
    for (auto &res : x.results)
    {
        result.results.push_back(get_low_prec<LReal, LComplex, Real, Complex>(res));
    }
    return result;
}

template <typename Real, typename Complex>
struct FindRootResult
{
    bool success;
    std::string fail_reason;
    std::optional<ForwardRayTracingResult<Real, Complex>> root;
};

template <typename Real, typename Complex>
class RootFunctor
{
private:
    using Vector = Eigen::Vector<Real, 2>;

    ForwardRayTracingParams<Real> &params;
    const Real theta_o;
    const Real phi_o;
    const int period;
    const bool fixed_period;
    const Real two_pi = boost::math::constants::two_pi<Real>();

public:
    std::shared_ptr<ForwardRayTracing<Real, Complex>> ray_tracing = ForwardRayTracing<Real, Complex>::get_from_cache();

    RootFunctor(ForwardRayTracingParams<Real> &params_, Real theta_o_, Real phi_o_)
        : params(params_),
          period(std::numeric_limits<int>::max()),
          fixed_period(false),
          theta_o(std::move(
              theta_o_)),
          phi_o(std::move(phi_o_))
    {
        ray_tracing->calc_t_f = false;
    }

    RootFunctor(ForwardRayTracingParams<Real> &params_, int period_, Real theta_o_, Real phi_o_)
        : params(params_),
          period(period_),
          fixed_period(true),
          theta_o(std::move(
              theta_o_)),
          phi_o(std::move(phi_o_))
    {
        ray_tracing->calc_t_f = false;
    }

    Vector operator()(const Vector &x)
    {
        auto &rc = x[0];
        auto &log_abs_d = x[1];
        params.rc = rc;
        params.log_abs_d = log_abs_d;
        params.rc_d_to_lambda_q();
        ray_tracing->calc_ray(params);

        if (ray_tracing->ray_status != RayStatus::NORMAL)
        {
            if (params.print_args_error || ray_tracing->ray_status != RayStatus::ARGUMENT_ERROR)
            {
                fmt::println("ray status: {}", ray_status_to_str(ray_tracing->ray_status));
            }
            return Vector::Constant(std::numeric_limits<Real>::quiet_NaN());
        }

        Vector residual;
        residual[0] = ray_tracing->theta_f - theta_o;
        if (fixed_period)
        {
            residual[1] = ray_tracing->phi_f - phi_o - period * two_pi;
        }
        else
        {
            residual[1] = sin((ray_tracing->phi_f - phi_o) * half<Real>());
        }

#ifdef PRINT_DEBUG
        fmt::println("rc: {}, log_abs_d: {}, theta_f: {}, phi_f: {}", x[0], x[1], ray_tracing->theta_f, ray_tracing->phi_f);
        fmt::println("residual: {}, {}", residual[0], residual[1]);
#endif
        return residual;
    }
};

template <typename T>
int sgn(T val)
{
    return (T(0) < val) - (val < T(0));
}

// move phi to [0, 2pi)
template <typename T>
void wrap_phi(T &phi)
{
    const T &two_pi = boost::math::constants::two_pi<T>();
    if (phi < 0 || phi >= two_pi)
    {
        phi -= two_pi * MY_FLOOR<T>::convert(phi / two_pi);
    }
}

template <typename Real, typename Complex>
struct ForwardRayTracingUtils
{
    static ForwardRayTracingResult<Real, Complex> calc_ray(const ForwardRayTracingParams<Real> &params)
    {
        auto ray_tracing = ForwardRayTracing<Real, Complex>::get_from_cache();
        ray_tracing->calc_ray(params);
        return ray_tracing->to_result();
    }

    static std::vector<ForwardRayTracingResult<Real, Complex>>
    calc_ray_batch(const std::vector<ForwardRayTracingParams<Real>> &params_list)
    {
        std::vector<ForwardRayTracingResult<Real, Complex>> results(params_list.size());
        oneapi::tbb::parallel_for(oneapi::tbb::blocked_range<size_t>(0u, params_list.size()),
                                  [&](const oneapi::tbb::blocked_range<size_t> &r)
                                  {
                                      for (size_t i = r.begin(); i != r.end(); ++i)
                                      {
                                          auto ray_tracing = ForwardRayTracing<Real, Complex>::get_from_cache();
                                          ray_tracing->calc_ray(params_list[i]);
                                          results[i] = ray_tracing->to_result();
                                      }
                                  });
        return results;
    }

    static FindRootResult<Real, Complex>
    find_root_period(const ForwardRayTracingParams<Real> &params, int period, Real theta_o, Real phi_o, Real tol)
    {
        wrap_phi(phi_o);
        ForwardRayTracingParams<Real> local_params(params);

        Eigen::Vector<Real, 2> x = Eigen::Vector<Real, 2>::Zero(2);
        x << local_params.rc, local_params.log_abs_d;

        auto root_functor =
            period == std::numeric_limits<int>::max() ? RootFunctor<Real, Complex>(local_params, theta_o, phi_o)
                                                      : RootFunctor<Real, Complex>(local_params, period,
                                                                                   std::move(theta_o),
                                                                                   std::move(phi_o));

        BroydenDF<Real, 2, RootFunctor<Real, Complex>> solver;
        AlgoParams<Real, 2> settings;
#ifdef PRINT_DEBUG
        settings.print_level = 1;
#endif
        solver.broyden_df(x, root_functor, settings);

        auto residual = root_functor(x);
        FindRootResult<Real, Complex> result;

        if (root_functor.ray_tracing->ray_status != RayStatus::NORMAL)
        {
            result.success = false;
            result.fail_reason = fmt::format("ray status: {}", ray_status_to_str(root_functor.ray_tracing->ray_status));
            return result;
        }

        if (residual.norm() > tol)
        {
            result.success = false;
            result.fail_reason = fmt::format("residual > threshold: {} > {}", residual.norm(), tol);
            return result;
        }

        result.success = true;
        result.root = root_functor.ray_tracing->to_result();

        auto &root = (*result.root);
        root.rc = x[0];
        root.log_abs_d = x[1];
        root.d_sign = local_params.d_sign;

        return result;
    }

    static FindRootResult<Real, Complex>
    find_root(const ForwardRayTracingParams<Real> &params, Real theta_o, Real phi_o, Real tol)
    {
        wrap_phi(phi_o);
        return find_root_period(params, std::numeric_limits<int>::max(), theta_o, phi_o, tol);
    }

    static ForwardRayTracingResult<Real, Complex> refine_result(ForwardRayTracingResult<Real, Complex> &res)
    {
    }

    static SweepResult<Real, Complex>
    sweep_rc_d_high(const ForwardRayTracingParams<Real> &params, Real theta_o, Real phi_o, const std::vector<Real> &rc_list,
                    const std::vector<Real> &lgd_list, size_t cutoff, Real tol)
    {
        using HReal = typename HigherPrecision<Real>::Type;
        using HComplex = typename HigherPrecision<Complex>::Type;

        ForwardRayTracingParams<HReal> params_h = params.template get_high_prec<HReal>();
        HReal theta_o_h = theta_o;
        HReal phi_o_h = phi_o;
        std::vector<HReal> rc_list_h(rc_list.size());
        for (size_t i = 0; i < rc_list.size(); i++)
        {
            rc_list_h[i] = rc_list[i];
        }
        std::vector<HReal> lgd_list_h(lgd_list.size());
        for (size_t i = 0; i < lgd_list.size(); i++)
        {
            lgd_list_h[i] = lgd_list[i];
        }
        HReal tol_h = tol;

        auto result = ForwardRayTracingUtils<HReal, HComplex>::sweep_rc_d(params_h, theta_o_h, phi_o_h, rc_list_h,
                                                                          lgd_list_h, cutoff, tol_h);
        return get_low_prec<Real, Complex, HReal, HComplex>(result);
    }

    static SweepResult<Real, Complex>
    sweep_rc_d(const ForwardRayTracingParams<Real> &params, Real theta_o, Real phi_o, const std::vector<Real> &rc_list,
               const std::vector<Real> &lgd_list, size_t cutoff, Real tol)
    {
        wrap_phi(phi_o);
        size_t rc_size = rc_list.size();
        size_t lgd_size = lgd_list.size();

        SweepResult<Real, Complex> sweep_result;

        auto &theta = sweep_result.theta;
        auto &phi = sweep_result.phi;

        auto &delta_theta = sweep_result.delta_theta;
        auto &delta_phi = sweep_result.delta_phi;

        auto &lambda = sweep_result.lambda;
        auto &eta = sweep_result.eta;

        theta.resize(lgd_size, rc_size);
        phi.resize(lgd_size, rc_size);

        delta_theta.resize(lgd_size, rc_size);
        delta_phi.resize(lgd_size, rc_size);

        lambda.resize(lgd_size, rc_size);
        eta.resize(lgd_size, rc_size);

        // rc and d
        oneapi::tbb::parallel_for(oneapi::tbb::blocked_range2d<size_t>(0u, lgd_size, 0u, rc_size),
                                  [&](const oneapi::tbb::blocked_range2d<size_t, size_t> &r)
                                  {
                                      auto ray_tracing = ForwardRayTracing<Real, Complex>::get_from_cache();
                                      Real two_pi = boost::math::constants::two_pi<Real>();
                                      ForwardRayTracingParams<Real> local_params(params);
                                      for (size_t i = r.rows().begin(); i != r.rows().end(); ++i)
                                      {
                                          for (size_t j = r.cols().begin(); j != r.cols().end(); ++j)
                                          {
                                              local_params.rc = rc_list[j];
                                              local_params.log_abs_d = lgd_list[i];
                                              local_params.rc_d_to_lambda_q();
                                              ray_tracing->calc_ray(local_params);
                                              if (ray_tracing->ray_status == RayStatus::NORMAL)
                                              {
                                                  theta(i, j) = ray_tracing->theta_f;
                                                  phi(i, j) = ray_tracing->phi_f;
                                                  delta_theta(i, j) = theta(i, j) - theta_o;
                                                  delta_phi(i, j) = sin((phi(i, j) - phi_o) * half<Real>());
                                                  lambda(i, j) = ray_tracing->lambda;
                                                  eta(i, j) = ray_tracing->eta;
                                              }
                                              else
                                              {
                                                  theta(i, j) = std::numeric_limits<Real>::quiet_NaN();
                                                  phi(i, j) = std::numeric_limits<Real>::quiet_NaN();
                                                  delta_theta(i, j) = std::numeric_limits<Real>::quiet_NaN();
                                                  delta_phi(i, j) = std::numeric_limits<Real>::quiet_NaN();
                                                  lambda(i, j) = std::numeric_limits<Real>::quiet_NaN();
                                                  eta(i, j) = std::numeric_limits<Real>::quiet_NaN();
                                              }
                                          }
                                      }
                                  });

        namespace bg = boost::geometry;
        namespace bgi = boost::geometry::index;
        using Point = bg::model::point<int, 2, bg::cs::cartesian>;
        tbb::concurrent_vector<Point> theta_roots_index;
        tbb::concurrent_vector<Point> phi_roots_index;
        oneapi::tbb::parallel_for(oneapi::tbb::blocked_range2d<size_t>(1u, lgd_size, 1u, rc_size),
                                  [&](const oneapi::tbb::blocked_range2d<size_t, size_t> &r)
                                  {
                                      int d_row, d_col, d_row_lambda, d_col_lambda;
                                      for (size_t i = r.rows().begin(); i != r.rows().end(); ++i)
                                      {
                                          for (size_t j = r.cols().begin(); j != r.cols().end(); ++j)
                                          {
                                              d_row = sgn(delta_theta(i, j)) * sgn(delta_theta(i, j - 1));
                                              d_col = sgn(delta_theta(i, j)) * sgn(delta_theta(i - 1, j));
                                              if (!isnan(delta_theta(i, j)) && !isnan(delta_theta(i, j - 1)) &&
                                                  !isnan(delta_theta(i - 1, j)) &&
                                                  (d_row <= 0 || d_col <= 0))
                                              {
                                                  theta_roots_index.emplace_back(i, j);
                                              }
                                              d_row = sgn(delta_phi(i, j)) * sgn(delta_phi(i, j - 1));
                                              d_col = sgn(delta_phi(i, j)) * sgn(delta_phi(i - 1, j));
                                              d_row_lambda = sgn(lambda(i, j)) * sgn(lambda(i, j - 1));
                                              d_col_lambda = sgn(lambda(i, j)) * sgn(lambda(i - 1, j));
                                              if (!isnan(delta_phi(i, j)) && !isnan(delta_phi(i, j - 1)) &&
                                                  !isnan(delta_phi(i - 1, j)) &&
                                                  !isnan(lambda(i, j)) && !isnan(lambda(i, j - 1)) &&
                                                  !isnan(lambda(i - 1, j)) && d_row_lambda > 0 &&
                                                  d_col_lambda > 0 &&
                                                  (d_row <= 0 || d_col <= 0))
                                              {
                                                  phi_roots_index.emplace_back(i, j);
                                              }
                                          }
                                      }
                                  });

        if (theta_roots_index.empty() && phi_roots_index.empty())
        {
            return sweep_result;
        }

        auto &theta_roots = sweep_result.theta_roots;
        theta_roots.resize(theta_roots_index.size(), 2);
        for (size_t i = 0; i < theta_roots_index.size(); i++)
        {
            theta_roots(i, 0) = rc_list[theta_roots_index[i].template get<1>()];
            theta_roots(i, 1) = lgd_list[theta_roots_index[i].template get<0>()];
        }

        if (phi_roots_index.empty())
        {
            return sweep_result;
        }

        auto &phi_roots = sweep_result.phi_roots;
        phi_roots.resize(phi_roots_index.size(), 2);
        for (size_t i = 0; i < phi_roots_index.size(); i++)
        {
            phi_roots(i, 0) = rc_list[phi_roots_index[i].template get<1>()];
            phi_roots(i, 1) = lgd_list[phi_roots_index[i].template get<0>()];
        }

        std::vector<Point> theta_roots_closest_index;
        theta_roots_closest_index.reserve(theta_roots_index.size());
        std::vector<double> distances(theta_roots_index.size());
        bgi::rtree<Point, bgi::quadratic<16>> rtree(phi_roots_index);
        for (size_t i = 0; i < theta_roots_index.size(); i++)
        {
            Point p = theta_roots_index[i];
            rtree.query(bgi::nearest(p, 1), std::back_inserter(theta_roots_closest_index));
            distances[i] = bg::distance(p, theta_roots_closest_index.back());
        }

        // sort rows of theta_roots_closest_index by distances
        std::vector<size_t> indices(theta_roots_index.size());
        std::iota(indices.begin(), indices.end(), 0);
        std::sort(indices.begin(), indices.end(),
                  [&distances](size_t i1, size_t i2)
                  { return distances[i1] < distances[i2]; });

        auto &theta_roots_closest = sweep_result.theta_roots_closest;
        theta_roots_closest.resize(theta_roots_index.size(), 2);
        for (size_t i = 0; i < theta_roots_index.size(); i++)
        {
            theta_roots_closest(i, 0) = rc_list[theta_roots_closest_index[indices[i]].template get<1>()];
            theta_roots_closest(i, 1) = lgd_list[theta_roots_closest_index[indices[i]].template get<0>()];
        }

        // find results
        auto &results = sweep_result.results;
        cutoff = std::min(cutoff, indices.size());
        results.reserve(cutoff);
        std::mutex results_mutex;
        tbb::parallel_for(tbb::blocked_range<size_t>(0u, cutoff),
                          [&](const tbb::blocked_range<size_t> &r)
                          {
                              ForwardRayTracingParams<Real> local_params(params);
                              Real two_pi = boost::math::constants::two_pi<Real>();
                              for (size_t i = r.begin(); i != r.end(); ++i)
                              {
                                  size_t row = theta_roots_closest_index[indices[i]].template get<0>();
                                  size_t col = theta_roots_closest_index[indices[i]].template get<1>();
                                  local_params.rc = rc_list[col];
                                  local_params.log_abs_d = lgd_list[row];
                                  local_params.rc_d_to_lambda_q();
                                  int period = MY_FLOOR<Real>::convert(phi(row, col) / two_pi);
                                  auto root_res = find_root_period(local_params, period, theta_o, phi_o, tol);
                                  if (root_res.success)
                                  {
                                      auto root = *std::move(root_res.root);
                                      std::lock_guard<std::mutex> lock(results_mutex);
                                      results.push_back(std::move(root));
                                  }
                                  else
                                  {
                                      fmt::println("find root failed, rc = {}, log_abs_d = {}, reason: {}\n", rc_list[col], lgd_list[row], root_res.fail_reason);
                                  }
                              }
                          });

        std::vector<size_t> duplicated_index;
        for (size_t i = 0; i < results.size(); i++)
        {
            for (size_t j = i + 1; j < results.size(); j++)
            {
                if (abs(results[i].rc - results[j].rc) < tol &&
                    abs(results[i].log_abs_d - results[j].log_abs_d) < tol)
                {
                    duplicated_index.push_back(j);
                    break;
                }
            }
        }
        // remove duplicated results
        std::sort(duplicated_index.begin(), duplicated_index.end());
        for (size_t i = duplicated_index.size(); i > 0; i--)
        {
            results.erase(results.begin() + duplicated_index[i - 1]);
        }

        return sweep_result;
    }
};