#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "ObjectPool.h"
#include "ForwardRayTracing.h"

namespace py = pybind11;

template <typename Real, typename Complex>
void define_forward_ray_tracing(pybind11::module_ &mod, const char *name) {
  using RayTracing = ForwardRayTracing<Real, Complex>;
  py::class_<RayTracing, std::shared_ptr<RayTracing>>(mod, name)
      .def_readonly("a", &RayTracing::a)
      .def_readonly("rp", &RayTracing::rp)
      .def_readonly("rm", &RayTracing::rm)
      .def_readonly("r_s", &RayTracing::r_s)
      .def_readonly("theta_s", &RayTracing::theta_s)
      .def_readonly("r_o", &RayTracing::r_o)
      .def_readonly("r1", &RayTracing::r1)
      .def_readonly("r2", &RayTracing::r2)
      .def_readonly("r3", &RayTracing::r3)
      .def_readonly("r4", &RayTracing::r4)
      .def_readonly("r1_c", &RayTracing::r1_c)
      .def_readonly("r2_c", &RayTracing::r2_c)
      .def_readonly("r3_c", &RayTracing::r3_c)
      .def_readonly("r4_c", &RayTracing::r4_c)
      .def_readonly("t_f", &RayTracing::t_f)
      .def_readonly("theta_f", &RayTracing::theta_f)
      .def_readonly("phi_f", &RayTracing::phi_f)
      .def_readonly("m", &RayTracing::m)
      .def_readonly("n_half", &RayTracing::n_half)
      .def_readonly("ray_status", &RayTracing::ray_status);
}

template<typename Real, typename Complex>
struct PyForwardRayTracing {
  inline static object_pool<ForwardRayTracing<Real, Complex>> pool;

  static std::shared_ptr<ForwardRayTracing<Real, Complex>>
  ray_tracing_rc_d(Real a, Real r_s, Real theta_s, Real r_o, Sign nu_r, Sign nu_theta, const Real &rc, const Real &d) {
    auto ray_tracing = pool.create();
    ray_tracing->calc_ray_by_rc_d(a, r_s, theta_s, r_o, nu_r, nu_theta, rc, d);
    return ray_tracing;
  }

  static std::shared_ptr<ForwardRayTracing<Real, Complex>> ray_tracing_lambda_q(
      Real a, Real r_s, Real theta_s, Real r_o, Sign nu_r, Sign nu_theta, const Real &lambda, const Real &q) {
    auto ray_tracing = pool.create();
    ray_tracing->calc_ray_by_lambda_q(a, r_s, theta_s, r_o, nu_r, nu_theta, lambda, q);
    return ray_tracing;
  }

  static void clean_cache() {
    pool.clear();
  }
};

PYBIND11_MODULE(py_forward_ray_tracing, mod) {
  py::enum_<RayStatus>(mod, "RayStatus")
      .value("NORMAL", RayStatus::NORMAL)
      .value("CONFINED", RayStatus::CONFINED)
      .value("ETA_OUT_OF_RANGE", RayStatus::ETA_OUT_OF_RANGE)
      .value("THETA_OUT_OF_RANGE", RayStatus::THETA_OUT_OF_RANGE)
      .value("UNKOWN_ERROR", RayStatus::UNKOWN_ERROR)
      .export_values();

  py::enum_<Sign>(mod, "Sign")
      .value("POSITIVE", Sign::POSITIVE)
      .value("NEGATIVE", Sign::NEGATIVE)
      .export_values();

  mod.def("ray_tracing_rc_d", &PyForwardRayTracing<double, std::complex<double>>::ray_tracing_rc_d,
          py::call_guard<py::gil_scoped_release>());
  mod.def("ray_tracing_lambda_q", &PyForwardRayTracing<double, std::complex<double>>::ray_tracing_lambda_q,
          py::call_guard<py::gil_scoped_release>());
  mod.def("clean_cache", &PyForwardRayTracing<double, std::complex<double>>::clean_cache);

  define_forward_ray_tracing<double, std::complex<double>>(mod, "ForwardRayTracingFloat64");
  define_forward_ray_tracing<long double, std::complex<long double>>(mod, "ForwardRayTracingLongDouble");
#ifdef FLOAT128
  // define_forward_ray_tracing<boost::multiprecision::float128, boost::multiprecision::complex128>(mod, "ForwardRayTracingFloat128");
#endif
#ifdef BIGFLOAT
  // define_forward_ray_tracing<BigFloat, BigComplex>(mod, "ForwardRayTracingBigFloat");
#endif
}
