# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
from telemetry import story
from telemetry.page import shared_page_state

from page_sets.rendering import rendering_story


class ToughCanvasPage(rendering_story.RenderingStory):
  ABSTRACT_STORY = True

  def __init__(self,
               page_set,
               shared_page_state_class=shared_page_state.SharedPageState,
               name_suffix='',
               extra_browser_args=None):
    super(ToughCanvasPage, self).__init__(
        page_set=page_set,
        shared_page_state_class=shared_page_state_class,
        name_suffix=name_suffix,
        extra_browser_args=extra_browser_args)

  def RunNavigateSteps(self, action_runner):
    super(ToughCanvasPage, self).RunNavigateSteps(action_runner)
    action_runner.WaitForJavaScriptCondition(
        "document.readyState == 'complete'")

  def RunPageInteractions(self, action_runner):
    with action_runner.CreateInteraction('CanvasAnimation'):
      action_runner.Wait(5)


class MicrosoftFirefliesPage(ToughCanvasPage):
  BASE_NAME = 'microsoft_fireflies'
  # pylint: disable=line-too-long
  URL = 'http://ie.microsoft.com/testdrive/Performance/Fireflies/Default.html'


class GeoAPIsPage(ToughCanvasPage):
  BASE_NAME = 'geo_apis'
  URL = 'http://geoapis.appspot.com/agdnZW9hcGlzchMLEgtFeGFtcGxlQ29kZRjh1wIM'


class RunwayPage(ToughCanvasPage):
  BASE_NAME = 'runway'
  URL = 'http://runway.countlessprojects.com/prototype/performance_test.html'


class MicrosoftFishIETankPage(ToughCanvasPage):
  BASE_NAME = 'microsoft_fish_ie_tank'
  # pylint: disable=line-too-long
  URL = 'http://ie.microsoft.com/testdrive/Performance/FishIETank/Default.html'


class MicrosoftSpeedReadingPage(ToughCanvasPage):
  BASE_NAME = 'microsoft_speed_reading'
  # pylint: disable=line-too-long
  URL = 'http://ie.microsoft.com/testdrive/Performance/SpeedReading/Default.html'


class Kevs3DPage(ToughCanvasPage):
  BASE_NAME = 'kevs_3d'
  URL = 'http://www.kevs3d.co.uk/dev/canvask3d/k3d_test.html'


class MegiDishPage(ToughCanvasPage):
  BASE_NAME = 'megi_dish'
  URL = 'http://www.megidish.net/awjs/'


class ManInBluePage(ToughCanvasPage):
  BASE_NAME = 'man_in_blue'
  URL = 'http://themaninblue.com/experiment/AnimationBenchmark/canvas/'


class Mix10KPage(ToughCanvasPage):
  BASE_NAME = 'mix_10k'
  URL = 'http://mix10k.visitmix.com/Entry/Details/169'


class CraftyMindPage(ToughCanvasPage):
  BASE_NAME = 'crafty_mind'
  URL = 'http://www.craftymind.com/factory/guimark2/HTML5ChartingTest.html'


class ChipTunePage(ToughCanvasPage):
  BASE_NAME = 'chip_tune'
  URL = 'http://www.chiptune.com/starfield/starfield.html'


class JarroDoversonPage(ToughCanvasPage):
  BASE_NAME = 'jarro_doverson'
  URL = 'http://jarrodoverson.com/static/demos/particleSystem/'


class EffectGamesPage(ToughCanvasPage):
  BASE_NAME = 'effect_games'
  URL = 'http://www.effectgames.com/demos/canvascycle/'


class SpielzeugzPage(ToughCanvasPage):
  BASE_NAME = 'spielzeugz'
  URL = 'http://spielzeugz.de/html5/liquid-particles.html'


class HakimPage(ToughCanvasPage):
  BASE_NAME = 'hakim'
  URL = 'http://hakim.se/experiments/html5/magnetic/02/'


class MicrosoftSnowPage(ToughCanvasPage):
  BASE_NAME = 'microsoft_snow'
  URL = 'http://ie.microsoft.com/testdrive/Performance/LetItSnow/'


class MicrosoftWorkerFountainsPage(ToughCanvasPage):
  BASE_NAME = 'microsoft_worker_fountains'
  # pylint: disable=line-too-long
  URL = 'http://ie.microsoft.com/testdrive/Graphics/WorkerFountains/Default.html'


class MicrosoftTweetMapPage(ToughCanvasPage):
  BASE_NAME = 'microsoft_tweet_map'
  URL = 'http://ie.microsoft.com/testdrive/Graphics/TweetMap/Default.html'


class MicrosoftVideoCityPage(ToughCanvasPage):
  BASE_NAME = 'microsoft_video_city'
  URL = 'http://ie.microsoft.com/testdrive/Graphics/VideoCity/Default.html'


class MicrosoftAsteroidBeltPage(ToughCanvasPage):
  BASE_NAME = 'microsoft_asteroid_belt'
  # pylint: disable=line-too-long
  URL = 'http://ie.microsoft.com/testdrive/Performance/AsteroidBelt/Default.html'


class SmashCatPage(ToughCanvasPage):
  BASE_NAME = 'smash_cat'
  URL = 'http://www.smashcat.org/av/canvas_test/'


class BouncingBallsShadowPage(ToughCanvasPage):
  BASE_NAME = 'bouncing_balls_shadow'
  # pylint: disable=line-too-long
  URL = 'file://../tough_canvas_cases/canvas2d_balls_common/bouncing_balls.html?ball=image_with_shadow&back=image'


class BouncingBalls15Page(ToughCanvasPage):
  BASE_NAME = 'bouncing_balls_15'
  # pylint: disable=line-too-long
  URL = 'file://../tough_canvas_cases/canvas2d_balls_common/bouncing_balls.html?ball=text&back=white&ball_count=15'


class CanvasFontCyclerPage(ToughCanvasPage):
  BASE_NAME = 'canvas_font_cycler'
  URL = 'file://../tough_canvas_cases/canvas-font-cycler.html'


class CanvasAnimationNoClearPage(ToughCanvasPage):
  BASE_NAME = 'canvas_animation_no_clear'
  URL = 'file://../tough_canvas_cases/canvas-animation-no-clear.html'


class CanvasToBlobPage(ToughCanvasPage):
  BASE_NAME = 'canvas_to_blob'
  URL = 'file://../tough_canvas_cases/canvas_toBlob.html'


class ManyImagesPage(ToughCanvasPage):
  BASE_NAME = 'many_images'
  URL = 'file://../../../../chrome/test/data/perf/canvas_bench/many_images.html'


class CanvasArcPage(ToughCanvasPage):
  BASE_NAME = 'canvas_arcs'
  URL = 'file://../tough_canvas_cases/rendering_throughput/canvas_arcs.html'


class CanvasLinesPage(ToughCanvasPage):
  BASE_NAME = 'canvas_lines'
  URL = 'file://../tough_canvas_cases/rendering_throughput/canvas_lines.html'


class PutGetImageDataPage(ToughCanvasPage):
  BASE_NAME = 'put_get_image_data'
  URL = 'file://../tough_canvas_cases/rendering_throughput/put_get_image_data.html'


class FillShapesPage(ToughCanvasPage):
  BASE_NAME = 'fill_shapes'
  URL = 'file://../tough_canvas_cases/rendering_throughput/fill_shapes.html'


class StrokeShapesPage(ToughCanvasPage):
  BASE_NAME = 'stroke_shapes'
  URL = 'file://../tough_canvas_cases/rendering_throughput/stroke_shapes.html'


class BouncingClippedRectanglesPage(ToughCanvasPage):
  BASE_NAME = 'bouncing_clipped_rectangles'
  # pylint: disable=line-too-long
  URL = 'file://../tough_canvas_cases/rendering_throughput/bouncing_clipped_rectangles.html'


class BouncingGradientCirclesPage(ToughCanvasPage):
  BASE_NAME = 'bouncing_gradient_circles'
  # pylint: disable=line-too-long
  URL = 'file://../tough_canvas_cases/rendering_throughput/bouncing_gradient_circles.html'


class BouncingSVGImagesPage(ToughCanvasPage):
  BASE_NAME = 'bouncing_svg_images'
  # pylint: disable=line-too-long
  URL = 'file://../tough_canvas_cases/rendering_throughput/bouncing_svg_images.html'


class BouncingPNGImagesPage(ToughCanvasPage):
  BASE_NAME = 'bouncing_png_images'
  # pylint: disable=line-too-long
  URL = 'file://../tough_canvas_cases/rendering_throughput/bouncing_png_images.html'


# TODO(crbug.com/760553):remove this class after smoothness.tough_canvas_cases
# benchmark is completely replaced by rendering benchmarks
class ToughCanvasCasesPageSet(story.StorySet):

  """
  Description: Self-driven Canvas2D animation examples
  """

  def __init__(self):
    super(ToughCanvasCasesPageSet, self).__init__(
      archive_data_file='../data/tough_canvas_cases.json',
      cloud_storage_bucket=story.PARTNER_BUCKET)

    # Crashes on Galaxy Nexus. crbug.com/314131
    # TODO(rnephew): Rerecord this story.
    # self.AddStory(MicrosofFirefliesPage(self))

    page_classes = [
        GeoAPIsPage,
        RunwayPage,
        MicrosoftFishIETankPage,
        MicrosoftSpeedReadingPage,
        Kevs3DPage,
        MegiDishPage,
        ManInBluePage,
        Mix10KPage,
        CraftyMindPage,
        ChipTunePage,
        JarroDoversonPage,
        EffectGamesPage,
        SpielzeugzPage,
        HakimPage,
        MicrosoftSnowPage,
        MicrosoftWorkerFountainsPage,
        MicrosoftTweetMapPage,
        MicrosoftVideoCityPage,
        MicrosoftAsteroidBeltPage,
        SmashCatPage,
        BouncingBallsShadowPage,
        BouncingBalls15Page,
        CanvasFontCyclerPage,
        CanvasAnimationNoClearPage,
        CanvasToBlobPage,
        ManyImagesPage,
        CanvasArcPage,
        CanvasLinesPage,
        PutGetImageDataPage,
        FillShapesPage,
        StrokeShapesPage,
        BouncingClippedRectanglesPage,
        BouncingGradientCirclesPage,
        BouncingSVGImagesPage,
        BouncingPNGImagesPage
    ]

    for page_class in page_classes:
      self.AddStory(page_class(self))
