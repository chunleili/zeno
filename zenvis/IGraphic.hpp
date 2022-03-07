#pragma once

#include <memory>
#include <vector>
#include <string>
#include <map>

namespace zenvis {

struct IGraphic {
  virtual void draw() = 0;
  virtual ~IGraphic() = default;
  GLuint m_envmap;
  void SetEnvMap(GLuint inEnv)
  {
      m_envmap = inEnv;
  }
};

struct FrameData {
    std::map<std::string, std::unique_ptr<IGraphic>> graphics;
};

FrameData *current_frame_data();

extern std::vector<std::unique_ptr<FrameData>> frames;

}
