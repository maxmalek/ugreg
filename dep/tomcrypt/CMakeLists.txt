# Slim it down to the basics we need:
# - some hashes
# - base64 encode/decode

# No free lunch today please
add_definitions(-DLTC_NOTHING)
add_definitions(-DLTC_NO_PROTOTYPES)
add_definitions(-DLTC_NO_FILE)
add_definitions(-DLTC_NO_MATH)

# Add the stuff we need back in
add_definitions(-DLTC_SHA3)
add_definitions(-DLTC_SHA512)
add_definitions(-DLTC_SHA256)
add_definitions(-DLTC_BASE64)
add_definitions(-DLTC_BASE64_URL) # Not needed but refuses to compile otherwise
add_definitions(-DLTC_HASH_HELPERS)

#add_definitions(-DLTC_NO_MODES)
#add_definitions(-DLTC_NO_MACS)
#add_definitions(-DLTC_NO_PRNGS)
#add_definitions(-DLTC_NO_PK)
#add_definitions(-DLTC_NO_CURVES)
#add_definitions(-DLTC_NO_PKCS)

file(GLOB src *.c *.h)

add_library(tomcrypt ${src})
