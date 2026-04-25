# A Ruby custom type, scanned by the unknown-type validator.
Puppet::Type.newtype(:loadertest_type) do
  newparam(:name)
  newparam(:value)
end
