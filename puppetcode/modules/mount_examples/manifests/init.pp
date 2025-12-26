# Mount resource examples
class mount_examples {
  # Example: Mount a tmpfs for temporary files
  mount { '/mnt/tmpfs':
    ensure  => mounted,
    device  => 'tmpfs',
    fstype  => 'tmpfs',
    options => 'size=100M,mode=1777',
  }

  # Example: Define a mount in fstab without mounting
  # (useful for removable media)
  # mount { '/mnt/backup':
  #   ensure  => defined,
  #   device  => 'UUID=your-uuid-here',
  #   fstype  => 'ext4',
  #   options => 'defaults,noauto',
  #   pass    => 2,
  # }

  # Example: NFS mount (requires nfs-common package)
  # mount { '/mnt/nfs':
  #   ensure  => mounted,
  #   device  => 'server:/export/share',
  #   fstype  => 'nfs',
  #   options => 'rw,soft,intr',
  # }
}
