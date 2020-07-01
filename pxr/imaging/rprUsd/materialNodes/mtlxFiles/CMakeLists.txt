set(MTLX_FILES
    Patterns/rpr_noise2d_texture.mtlx
    Patterns/rpr_checker_texture.mtlx
    Patterns/rpr_gradient_texture.mtlx
    Patterns/rpr_dot_texture.mtlx

    Utilities/rpr_bump_map.mtlx
    Utilities/rpr_normal_map.mtlx
    Utilities/rpr_twosided.mtlx
    Utilities/rpr_blend_value.mtlx
    Utilities/rpr_constant_texture.mtlx

    Utilities/rpr_rgb_to_hsv.mtlx
    Utilities/rpr_hsv_to_rgb.mtlx

    Utilities/rpr_input_lookup.mtlx

    Utilities/rpr_uv_triplanar.mtlx
    Utilities/rpr_uv_procedural.mtlx

    Utilities/rpr_fresnel.mtlx
    Utilities/rpr_fresnel_schlick.mtlx

    Shaders/rpr_add.mtlx
    Shaders/rpr_blend.mtlx

    Shaders/rpr_uber.mtlx
    Shaders/rpr_diffuse.mtlx
    Shaders/rpr_diffuse_refraction.mtlx
    Shaders/rpr_emissive.mtlx
    Shaders/rpr_microfacet.mtlx
    Shaders/rpr_microfacet_anisotropic_reflection.mtlx
    Shaders/rpr_microfacet_anisotropic_refraction.mtlx
    Shaders/rpr_microfacet_beckmann.mtlx
    Shaders/rpr_microfacet_refraction.mtlx
    Shaders/rpr_orennayar.mtlx
    Shaders/rpr_passthrough.mtlx
    Shaders/rpr_phong.mtlx
    Shaders/rpr_reflection.mtlx
    Shaders/rpr_refraction.mtlx
    Shaders/rpr_transparent.mtlx
    Shaders/rpr_volume.mtlx
    Shaders/rpr_ward.mtlx
)

foreach(mtlx ${MTLX_FILES})
    get_filename_component(dir ${mtlx} DIRECTORY)
    install(
        FILES ${mtlx}
        DESTINATION materials/${dir})
endforeach()
