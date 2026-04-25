# Bad sources: missing file in real module + completely missing module
node default {
  file { '/tmp/missing-file':
    source => 'puppet:///modules/srctest/no-such-file.txt',
  }
  file { '/tmp/missing-mod':
    source => 'puppet:///modules/no-such-module/anything',
  }
}
