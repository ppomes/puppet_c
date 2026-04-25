class mymod {
  # Bare key — module_name should resolve to "mymod" via caller class
  $bare = hiera('plain_key')
  # Key resolved purely from module_name (the namespaced flow)
  $shared = hiera('mymod::shared')
  notify { "mymod-result":
    message => "bare=${bare} shared=${shared}",
  }
}
