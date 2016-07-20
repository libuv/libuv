find_library(KVM_LIBRARY NAMES kvm)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(KVM DEFAULT_MESSAGE KVM_LIBRARY)

if(KVM_FOUND)
    set(KVM_LIBRARIES ${KVM_LIBRARY})
endif()

mark_as_advanced(KVM_LIBRARIES)