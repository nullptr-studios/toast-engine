# Binary SDK, every file comes straight from the tagged NVIDIA/DLSS tree and is pinned by its hash
set(_dlss_files
    include/nvsdk_ngx.h 007f56bdddd85d8fdc1da81022137afb302bb1c0dba63b5e36f044b63f4d10858bff5f21d80e0d46d156ed34847cca92ec4830deb6ee1424091fbd1a5f4336ed
    include/nvsdk_ngx_defs.h 605bbd1465c356516ce02903775306ba61d65a08ec4ba21823dd55486ab71bd75cc15680562946990ba1cc4a7ac1ba78a00c46e8951c9cd3b7565fb4e6f3ee3f
    include/nvsdk_ngx_defs_dlssd.h 31d1ba8c8c3abd4601ef34d38f84648ca822226be3fd12e730a7625899f5ce4b137da1b1f90710d17109c886c1868f1d9d51f2498d31df1f508c9b2fda6c06ba
    include/nvsdk_ngx_defs_dlssg.h 14ab4cf01c33c52b22f7fb01710913ef40803e47629e2e6af0093f1134ad94dcc4c5ad9d3c85cd81d16d387d9b115937906edbeacfc72f98e3c318e6737b9154
    include/nvsdk_ngx_defs_vk.h 288f450317a64fa92e47e8e54b4981506d1bad6854d41d5b742aabf7536ad9d6240c62faab895b8b2763ce917762899cf267e3f2b1db12b4b5e7bd0364bf4919
    include/nvsdk_ngx_helpers.h 19f9f8c078ed310606aba35405b6b14f3ae4fa2f8c22dfecef3ff193a0498a2ce27a977a6e95287f17447886dea174a1bbfa3f9a9b766943f7bd7dd59ed0c64f
    include/nvsdk_ngx_helpers_cuda.h 79b5f682f89f9937ee5598b13cfe957afb459c3c1e48879376806c5fe2c0c82e8d78b6b49765cbc69ad871acf62bc2e563984f21770e21541eef07775b582119
    include/nvsdk_ngx_helpers_d3d.h 274724bf280738a3b3857028c9cea137a92035f14feb2038f9620287d91ba7c0164bf08502b53d60c871c64be6842cbf45618cf925276a698c26dfc88e01c266
    include/nvsdk_ngx_helpers_dlssd.h f845cdfcea7f0b635ae628cf697ea9c3032f4e8fb9ce87f52b3a057fb0303b18ca00733599aa393edb451cf9faf3ddf335bb90bc5571c3d641d5163692310289
    include/nvsdk_ngx_helpers_dlssd_cuda.h 2339c07310350ba5f6a53332d98078b648571e6850d0b7605c3723c9e3d68be75dd153de1d6b2db168d7aa0b0c38b7a55fc17bc42f79ec96e6cd8f36a2eb9e71
    include/nvsdk_ngx_helpers_dlssd_d3d.h db7371481d2f8577fe3e2696eb9d904d7ce7b42dd10ff4523fb4f58f722d22baf1e27d447b13d01761d72f969435508611171e1bfad4200edfbe38c7d65870da
    include/nvsdk_ngx_helpers_dlssd_vk.h 338c82cb435304c66585cf3fccd98279d7bf997156b35ff6e8cc95eed6a5ff8a68c0a6b475e7d452811a5649a5fc43776b4029584bb48db48fd1f0ec6f6b4070
    include/nvsdk_ngx_helpers_dlssg.h 59d049ad2ecb8c7ce7573624118871ae709fab3c4944941b5273810ebe715aed0e66cc9d44eec5c3c9238d9d95249e4727407859818ba7e5b821ef851fab3fd7
    include/nvsdk_ngx_helpers_dlssg_d3d.h c549728b24ccea6865e20bde54ca2fb0d2f486a1e941c27fe7516450ab15de1bd7d5d0a675fc2b4d90e37178dbea2d25b9bca76e25f19afe7a6374e20eeef77a
    include/nvsdk_ngx_helpers_dlssg_vk.h 00db89e091cf8203e702089611ffe8474359b3125f481523ab3d2a813f105756f1014dcff26aa507519bd7640368adae793dac8fe42ac532a043c9df4a926239
    include/nvsdk_ngx_helpers_vk.h 8051906d9baec8440cfeed0c6b2380cb269622b0a5ece8376e661e22531ade91ae807b4810081cdf4e56dd4aaab1c7a44e0f2eb004be5ef7f572eca10a68307a
    include/nvsdk_ngx_loader.h a4661533cccad7db70d589741c0e3c4a8e318c15e9dab5f88cc763f51ae8b959117b11f417d26f1b4b334a25cbeffaf2db24ed0a43b9264b50b42fe1fdcf0302
    include/nvsdk_ngx_params.h f8b58c7e42f1948d17de5dad5c25d8341b5cf5dcb160de06de48fa2ef12ae98171e2aba8e3bd7a8e3b5321937c15f9edce7dd6aafc4dbd3588c946247322d2a4
    include/nvsdk_ngx_params_dlssd.h d9ddbdbe0ba2f3624daf41dcf3185400ee034e7e0a057df4f9080b63bfd8ce074ef0024f34a80aa4b8c15d163fdbeeabca3da319be681dd06e27311cab2c2a53
    include/nvsdk_ngx_params_dlssg.h b446b2594b478cc31fa0d542ee00d83969690075c9c75e8341fc85a3bdad6cad252d52ccaa4ae34f4d673e16f6316bed6ceb69dea1f0085745cd4f758d563dc4
    include/nvsdk_ngx_standalone_common.h 7bd34e71dd346650d9194e3792fa0a95a5bef1fb1b514b678abebadbd4385a8ee6bcec75963ef66151dd4ec45220793dcc09bb984dd4af03fdf10abe4da71ac0
    include/nvsdk_ngx_standalone_cuda.h 7cefa9dd4c35c8e6ef6213ae90deecd3fe180d5482dac35800bc7c853f76a188841baff1c2e48343590bd6cc0d544337046601ce67b9146883ec28eb33310167
    include/nvsdk_ngx_vk.h 610fd37c9d059f939a1d9899662d9c8bf6f9b7059a22efdff78c4ef13466e211c51abef13f8098cb6b8c357a9574de617a1ca41bbe0a30107543d414e6eccecc
    LICENSE.txt 72f5b66daad2df8b49098857adc8705dae17b60fcbefb3f8625fd252aa1e5b82dd92cb9824dd052976c73f5a0d7437ca54e593b8e9a9010758aba8887f853390
)
if(VCPKG_TARGET_IS_WINDOWS)
    list(APPEND _dlss_files
        lib/Windows_x86_64/x64/nvsdk_ngx_d.lib 0809baf904b4246e2ecc35ffc4a5a88155f9c74722427a6b02d0fa85c5a4f24e9d9f759863e6fc3c7038927a14e003215f50215cda4dfbc7b4edb513e5bf58df
        lib/Windows_x86_64/x64/nvsdk_ngx_d_dbg.lib 781cbe38adffc42b47b31a047d73e9de7e77242a2ad0d50a787335920985b2b3160f025c509bb9a31750f6094aecb0dd80607e12ed3029ed9bc7e79b33ef4d3b
        lib/Windows_x86_64/rel/nvngx_dlss.dll 0e1fbd01ef2374f68e6ff8728f8b394279e58670c02d1f2aadbc8927db97476eb7bc8dca2a3dfd17d13789e6b5ef101850dec836a7fda3e67d379d4a34dfae7d
    )
else()
    list(APPEND _dlss_files
        lib/Linux_x86_64/libnvsdk_ngx.a ed292252d5a7f905f63b85ada671827300cf28d0cc18c6ae7679261289907518500cb5167991e90972cc1f412745dcd674281afaab60b4253095f0939ed1ce4a
        lib/Linux_x86_64/rel/libnvidia-ngx-dlss.so.${VERSION} e95c833c790c656d93392d08938616596776581da6b2e88426b75bec1fccba24ea838d3ba472fea22ab2405726a2dba63e2b1b6badc577068df4b388dcb9b7c5
    )
endif()

while(_dlss_files)
    list(POP_FRONT _dlss_files _path _sha512)
    get_filename_component(_leaf "${_path}" NAME)
    string(REPLACE "/" "-" _name "${_path}")
    vcpkg_download_distfile("_dlss_${_leaf}"
        URLS "https://raw.githubusercontent.com/NVIDIA/DLSS/v${VERSION}/${_path}"
        FILENAME "nvidia-dlss-${VERSION}-${_name}"
        SHA512 ${_sha512}
    )
    if(_path MATCHES "^include/")
        file(INSTALL "${_dlss_${_leaf}}" DESTINATION "${CURRENT_PACKAGES_DIR}/include/ngx" RENAME "${_leaf}")
    endif()
endwhile()

if(VCPKG_TARGET_IS_WINDOWS)
    file(INSTALL "${_dlss_nvsdk_ngx_d.lib}" DESTINATION "${CURRENT_PACKAGES_DIR}/lib" RENAME "nvsdk_ngx_d.lib")
    file(INSTALL "${_dlss_nvsdk_ngx_d_dbg.lib}" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib" RENAME "nvsdk_ngx_d_dbg.lib")
    # NVIDIA ships only a release runtime, NGX loads it at run time in every configuration
    file(INSTALL "${_dlss_nvngx_dlss.dll}" DESTINATION "${CURRENT_PACKAGES_DIR}/bin" RENAME "nvngx_dlss.dll")
    file(INSTALL "${_dlss_nvngx_dlss.dll}" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/bin" RENAME "nvngx_dlss.dll")
else()
    # One static loader for both configurations, the runtime keeps NVIDIA's versioned file name because NGX looks for it
    file(INSTALL "${_dlss_libnvsdk_ngx.a}" DESTINATION "${CURRENT_PACKAGES_DIR}/lib" RENAME "libnvsdk_ngx.a")
    file(INSTALL "${_dlss_libnvsdk_ngx.a}" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib" RENAME "libnvsdk_ngx.a")
    file(INSTALL "${_dlss_libnvidia-ngx-dlss.so.${VERSION}}" DESTINATION "${CURRENT_PACKAGES_DIR}/lib" RENAME "libnvidia-ngx-dlss.so.${VERSION}")
    file(INSTALL "${_dlss_libnvidia-ngx-dlss.so.${VERSION}}" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib" RENAME "libnvidia-ngx-dlss.so.${VERSION}")
endif()

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/nvidia-dlss-config.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${_dlss_LICENSE.txt}")
