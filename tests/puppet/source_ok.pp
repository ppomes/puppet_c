# Valid sources: file and directory under modules/srctest/files
node default {
  file { '/tmp/file-source':
    source => 'puppet:///modules/srctest/data.txt',
  }
  file { '/tmp/dir-source':
    source  => 'puppet:///modules/srctest/sub',
    recurse => true,
  }
}
