# Top-level class for autoload tests.
class loadertest($greeting = 'hello') {
  $modlocal = 'set-by-loadertest'
  notify { "loadertest-greet":
    message => "${greeting} from loadertest, modlocal=${modlocal}",
  }
}
