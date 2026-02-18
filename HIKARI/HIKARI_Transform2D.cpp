#include "HIKARI_Transform2D.h"


namespace HIKARI {


    Matrix3x3 Transform2D::ToWorld(float /*width*/, float /*height*/) const {

        Matrix3x3 tNegPivot = Matrix3x3::MakeTranslate(-pivotPx.x, -pivotPx.y);

        Matrix3x3 s = Matrix3x3::MakeScale(scale.x, scale.y);

        Matrix3x3 r = Matrix3x3::MakeRotate(rotation);

        Matrix3x3 tPos = Matrix3x3::MakeTranslate(position.x, position.y);

        return tNegPivot * s * r * tPos;
    }


} // namespace HIKARI