/* stb_image implementation.
 *
 * Third-party code, compiled once here without warnings (see CMakeLists.txt).
 * The engine only uses it through image_loader.c.
 */

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO          /* Files are read by the engine (core/file.c) */
#define STBI_FAILURE_USERMSG   /* Readable error messages                    */
#include <stb_image.h>