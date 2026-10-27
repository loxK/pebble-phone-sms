import os

top = '.'
out = 'build'


def options(ctx):
    ctx.load('pebble_sdk')


def configure(ctx):
    ctx.load('pebble_sdk')


def build(ctx):
    # tools/shoot_screenshots.sh sets this to a Unix time: the build then answers
    # with made-up data dated from it instead of talking to the phone.
    demo_now = os.environ.get('PEBBLE_PHONE_DEMO')
    if demo_now:
        # The made-up portraits, written by tools/make_demo_photos.py, stay out of
        # package.json and so out of the published app. pebble_sdk reads the
        # resource list of each platform when it loads.
        for platform in ctx.env.TARGET_PLATFORMS:
            env = ctx.all_envs[platform]
            env.RESOURCES_JSON = env.RESOURCES_JSON + [
                {'type': 'raw', 'name': 'DEMO_PORTRAITS', 'file': 'demo/portraits.bin'}]
    ctx.load('pebble_sdk')

    binaries = []
    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform].derive()
        if demo_now:
            ctx.env.append_value('DEFINES', 'DEMO_NOW={}'.format(int(demo_now)))
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        ctx.pbl_build(source=ctx.path.ant_glob('src/c/**/*.c'), target=app_elf, bin_type='app')
        binaries.append({'platform': platform, 'app_elf': app_elf})
    ctx.env = cached_env

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries)
