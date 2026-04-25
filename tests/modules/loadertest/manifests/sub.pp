# Sub-class to exercise the namespaced autoload path.
class loadertest::sub {
  notify { "loadertest-sub":
    message => "loadertest::sub loaded",
  }
}
